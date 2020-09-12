;; Quite possibly the simplest generator: slap "#include {arg}" into the source file
;; These need to be simple in order to make it easy for external build tools to parse them
;; Otherwise, they'd support multiple includes per invocation, etc.
(include "<stdio.h>")
(include "<vector>" :with-decls)
;; (include "Test.cake")

(var my-module-var int 5)

;; The defun generator will implicitly invoke a (generate-args) generator on the args list. "int"
;;  isn't a generator or a function, it's a DSL symbol generate-args understands
(defun main (int arg-count ([] (* char)) args &return int)
  ;; (printf 1 2)
  (printf "This is a test. Here's a number: %d" 4)
  (when (= (square (square 2)) 16)
    (printf "Woo hoo"))
  (while (not true)
    (when (and (= true false) (= 0 1))
        (printf "Bad things"))
    (printf "Test")
    (return 1))
  (return 0))

(global-var unitialized int)

(defun-local helper ((rval-ref-to std::string) copy-string  &return std::string)
  (scope
   (var temp (& std::string) (std::move copy-string))
   (set temp "crazy"))
  (static-var numbers ([] 5 int) (array 0))
  (return (array 1 2 3 (bit-or 1 (bit-<< 1 2))))
  (printf "%p" (addr temp))
  (return "test"))

(defun test-complex-args ((* (const char)) filename
                          ;; Need to upcase singular argument type names manually (see PascalCaseIfPlural comments)
                          (& (const (<> std::vector Token))) tokens
                          int start-token-index
                          ;; generate-operation is a C++ type, but because of PascalCaseIfPlural,
                          ;; the lispy version becomes GenerateOperation. It's probably a good idea
                          ;; to write GenerateOperation here instead, because then ETAGS etc. can
                          ;; still find the C++ definition without running our conversion functions
                          (& (<> std::vector (* generate-operation) other-thing)) operations-out
                          &return int)
  (return 2))

(defun test-array-args (([] int) numbers ([] ([] 5 char)) five-letter-words
                        ;; ([] 4 ([] 4 float)) matrix ;; C doesn't like this, but the Cakelisp type system can handle it
                        &return (* char))
  (return (nth (test-complex-args) (def-after) five-letter-words)))

(defun def-after (&return int)
  (return 1))

;; Terminology notes: :thing is a keyword symbol. &thing is a symbol or marker symbol (maybe I call it a sentinel?)

;; Becomes add() once converted
(defun + (int a int b &return int)
  (return 0))

(defun vec-+ (int a int b &return int)
  (return 0))

;; Test max buffer length handling
;; (defun ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff (int a int b &return int)
  ;; (return 0))

;; (square (defun oh-no (&return std::string)(return "Hello macros!")))

(defun variable-declaration-generator ((& EvaluatorEnvironment) environment
                                       (& (const EvaluatorContext)) context
                                       (& (const (<> std::vector Token))) tokens
                                       int start-token-index
                                       (& GeneratorOutput) output)
  (when (IsForbiddenEvaluatorScope "variable declaration" (nth start-token-index tokens)
                                   context EvaluatorScope_ExpressionsOnly)
    (return false))
  (var name-token-index int (+ start-token-index 1))
  (return true))

(defun test-macro-magic (&return int)
  (return (the-answer)))

(defmacro the-answer ()
  (var startToken (& (const Token)) (at startTokenIndex tokens))
  (output.push_back (array TokenType_Symbol "42"
                           startToken.source startToken.lineNumber
	                       startToken.columnStart startToken.columnEnd))
  (return true))

(defun the-answer-fun ()
  (return 42))
