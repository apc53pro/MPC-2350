#include "globals.h"

#ifndef TERMINAL_FUNCS_H
#define TERMINAL_FUNCS_H

int precedenceCheck(char op);

int minZero(int n);

double mathsSolver(String expr);

String ARGUMENT_STRUCTURE_ANALYZER(String arg, String argStructure, int returnContentField);

bool isValidSymbol(char ch);


double PRINT(String code);

int variableFormatValidator(String varName);

int8_t varTypeLUT(String varType);

int8_t terminal_funcs(String code);

#endif
