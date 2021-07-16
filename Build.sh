#!/bin/sh

CAKELISP_BOOTSTRAP_BIN=bin/cakelisp_bootstrap
CC=g++
LINK=g++

# Note: If you make changes to the bootstrap process, you will need to delete
#  $CAKELISP_BOOTSTRAP_BIN so that it is updated

if test -f "$CAKELISP_BOOTSTRAP_BIN"; then
	echo "$CAKELISP_BOOTSTRAP_BIN exists. Building using Cakelisp"
	$CAKELISP_BOOTSTRAP_BIN Bootstrap.cake || exit $?
	echo "Use ./bin/cakelisp to build your files"
else
	echo "$CAKELISP_BOOTSTRAP_BIN does not exist. Building bootstrap executable manually"
	mkdir -p bin
	$CC -std=c++11 -c \
		src/Tokenizer.cpp \
		src/Evaluator.cpp \
		src/Utilities.cpp \
		src/FileUtilities.cpp \
		src/Converters.cpp \
		src/Writer.cpp \
		src/Generators.cpp \
		src/GeneratorHelpers.cpp \
		src/RunProcess.cpp \
		src/OutputPreambles.cpp \
		src/DynamicLoader.cpp \
		src/ModuleManager.cpp \
		src/Logging.cpp \
		src/Build.cpp \
		src/Main.cpp \
		-DUNIX || exit $?
	# Need -ldl for dynamic loading, --export-dynamic to let compile-time functions resolve to
	# Cakelisp symbols
	$LINK -o $CAKELISP_BOOTSTRAP_BIN *.o -ldl -rdynamic || exit $?
	rm *.o
	echo "Built $CAKELISP_BOOTSTRAP_BIN successfully. Now building with Cakelisp"
	$CAKELISP_BOOTSTRAP_BIN Bootstrap.cake || exit $?
	echo "Cakelisp successfully bootstrapped. Use ./bin/cakelisp to build your files"
fi
