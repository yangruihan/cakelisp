(skip-build)

;; Use a macro so that the type token address can be used
;; Note that this causes the caller's function to return false if the binding failed
;; TODO: Default initialization?
(defmacro get-or-create-comptime-var ()
  (unless (field environment moduleManager)
    (return false))

  (destructure-arguments bound-var-name-index var-type-index)
  (quick-token-at bound-var-name bound-var-name-index)
  (quick-token-at var-type var-type-index)

  (unless (ExpectTokenType "get-or-create-comptime-var" bound-var-name TokenType_Symbol)
    (return false))

  (var var-type-str Token var-type)
  (set (field var-type-str type) TokenType_String)
  ;; Convert type to string
  (scope
   (var type-to-string-buffer ([] 128 char) (array 0))
   (var type-string-write-head (* char) type-to-string-buffer)
   (var current-type-token (* (const Token)) (addr var-type))
   (var end-type-token (* (const Token)) (FindTokenExpressionEnd current-type-token))
   (while (<= current-type-token end-type-token)
     (appendTokenToString (deref current-type-token) (addr type-string-write-head) type-to-string-buffer (sizeof type-to-string-buffer))
     (incr current-type-token))
   (set (field var-type-str contents) type-to-string-buffer))

  (var var-name Token bound-var-name)
  (set (field var-name type) TokenType_String)

  ;; TODO: Only do this if one isn't already defined
  (scope ;; Create destruction function
   (var destruction-func-def (* (<> std::vector Token)) (new (<> std::vector Token)))
   ;; Need to have the environment delete this once it's safe
   (on-call (field environment comptimeTokens) push_back destruction-func-def)
   (tokenize-push (deref destruction-func-def)
                  (defun-comptime destroy-me (data (* void))
                    (delete (type-cast data (* (token-splice-addr var-type))))
                    (return true)))

   (var destruction-func-context EvaluatorContext context)
   (set (field destruction-func-context isRequired) true)
   (set (field destruction-func-context scope) EvaluatorScope_Module)

   (set (field destruction-func-context definitionName)
        (addr (path environment . moduleManager > globalPseudoInvocationName)))
   ;; We are only outputting a compile-time function, which uses definition's output storage to be
   ;; built. This throwaway will essentially only have a splice to that output, so we don't really
   ;; need to keep track of it, except to destroy it once everything is done
   (var throwaway-output (* GeneratorOutput) (new GeneratorOutput))
   (on-call (field environment orphanedOutputs) push_back  throwaway-output)
   (unless (= 0 (EvaluateGenerate_Recursive environment
                                            destruction-func-context
                                            (deref destruction-func-def) 0
                                            (deref throwaway-output)))
     (return false)))

  ;; Create the binding and lazy-variable creation
  ;; TODO: Any way to make this less code for each ref? There's a lot here
  (tokenize-push output
                 (var (token-splice-addr bound-var-name) (* (token-splice-addr var-type)) nullptr)
                 (unless (GetCompileTimeVariable environment
                                                 (token-splice-addr var-name) (token-splice-addr var-type-str)
                                                 (type-cast (addr (token-splice-addr bound-var-name)) (* (* void))))
                   (set (token-splice-addr bound-var-name) (new (token-splice-addr var-type)))
                   ;; TODO: Define destruction function
                   (var destroy-func-name (* (const char)) "destroy-me")
                   (unless (CreateCompileTimeVariable environment
                                                      (token-splice-addr var-name) (token-splice-addr var-type-str)
                                                      (type-cast (token-splice-addr bound-var-name) (* void))
                                                      destroy-func-name)
                     (delete (token-splice-addr bound-var-name))
                     (return false))))
  (return true))

;; TODO: This should be builtin to macros and generators
(defmacro destructure-arguments ()
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  ;; Find the end invocation for the caller, not us
  (tokenize-push output
                 (var destr-end-invocation-index int
                        (FindCloseParenTokenIndex tokens startTokenIndex)))
  (var start-args-index int (+ 2 startTokenIndex))
  (var current-arg-index int start-args-index)
  ;; Invocation is 0, so skip it
  (var num-destructured-args int 1)
  (while (< current-arg-index end-invocation-index)
    (var current-arg (* (const Token)) (addr (at current-arg-index tokens)))
    (var num-destructured-args-token Token (array TokenType_Symbol (std::to_string num-destructured-args)
                                                  "test/Macros.cake" 1 1 1))
    (unless (ExpectTokenType "destructure-arguments" (at current-arg-index tokens) TokenType_Symbol)
      (return false))
    (var destructured-arg-name-token Token (array TokenType_String (field (at current-arg-index tokens) contents)
                                                  "test/Macros.cake" 1 1 1))
    (tokenize-push output
                   (var (token-splice current-arg) int
                        (getExpectedArgument
                         ;; Use the name of the requested argument as the message
                         (token-splice (addr destructured-arg-name-token))
                         tokens startTokenIndex
                         (token-splice (addr num-destructured-args-token))
                         destr-end-invocation-index))
                   (when (= -1 (token-splice current-arg)) (return false)))
    (++ num-destructured-args)
    (set current-arg-index
         (getNextArgument tokens current-arg-index end-invocation-index)))
  (return true))

;; Assumes tokens is the array of tokens
(defmacro quick-token-at ()
  (destructure-arguments name index)
  (tokenize-push output (var (token-splice (addr (at name tokens))) (& (const Token))
                             (at (token-splice (addr (at index tokens))) tokens)))
  (return true))

(defmacro array-size ()
  (destructure-arguments array-index)
  (quick-token-at array-token array-index)
  ;; This should evaluate its argument, but I'm just hacking it in right now anyways
  (unless (ExpectTokenType "array-size" array-token TokenType_Symbol)
    (return false))
  (tokenize-push output (/ (sizeof (token-splice (addr array-token)))
                           (sizeof (at 0 (token-splice (addr array-token))))))
  (return true))
