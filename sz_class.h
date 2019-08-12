/*	
	sz_class.c header file
	contans all prototypes and sz libaries needed for sz_class.c
*/

//#ifndef SZCLASS_H
//#define SZCLASS_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "sz.h"
#include "rw.h"
#include <string.h>

typedef struct statistics {
	double *compRatio;
	double *compTime;
	double *decompTime;
} stats;

typedef struct variables {
	char varName[20];
	size_t numVectors;
	size_t dimensions[5];		//dimensions go 1d -> 5d, if only 1d set first to D and rest to 0
	double *regVarBuffer;
	unsigned char **dataVectors;
	size_t *numBytes;
	stats **compStats;
} variable;

typedef struct compressedMem {
	variable **varArray;		//default
	int maxNumVars;
	int varCount;
} compMem;

void cost_start(void);
void cost_end(void);
void sz_getStats(double *stats, int d, char *varName);
void sz_printVarInfo(char *varName);
void sz_constructor(char *cfgFile, int numVars);
void sz_destructor(void);
void sz_registerVar(char *varName, size_t numVectors, int d5,  int d4, int d3, int d2, int d1);
void sz_comp(double *data, int size, char *varName, int dvIndex, int errBoundMode, double errBound, int compType);
void sz_decomp(double *decompData, int out, char *varName, int dvIndex, int compType);

//#endif /* SZCLASS_H */