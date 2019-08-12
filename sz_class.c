/*
    sz_class.c
    contains all function that are called from python py_sz_class.py using SWIG

    sz features that must be enabled:
        temporal compression from ./configure
        add -fPIC to CFLAGS from ./configure
*/

#include <time.h>
#include "zfp.h"
#include "sz_class.h"
 
//global memory
compMem *memory;                //global "memory" used in storage of all sz/truncation data

//zfp globals (only working with one variable "L.u[]")
#define ZFP_NUM_STREAM 10   //set based on num_nodes from pySDC
#define DATA_LEN 262143     //set based on nvars from pySDC
zfp_field* field[ZFP_NUM_STREAM];  /* array meta data */
zfp_stream* zfp[ZFP_NUM_STREAM];   /* compressed stream */
bitstream* stream[ZFP_NUM_STREAM] = {NULL}; /* bit stream to write to or read from */
void* buffer[ZFP_NUM_STREAM] = {NULL};
double oArray[DATA_LEN];
zfp_type type = zfp_type_double;

//timing globals
struct timeval startTime;
struct timeval endTime;         //Start and end times
struct timeval costStart;       //only used for recording the cost
double totalCost = 0;

//timing functions
void cost_start(void) {
    totalCost = 0;
    gettimeofday(&costStart, NULL);
}
 
void cost_end(void) {
    double elapsed;
    struct timeval costEnd;
    gettimeofday(&costEnd, NULL);
    elapsed = ((costEnd.tv_sec*1000000+costEnd.tv_usec)-(costStart.tv_sec*1000000+costStart.tv_usec))/1000000.0;
    totalCost += elapsed;
}

//d is 3 times the numVectors (M), comes from python
void sz_getStats(double *stats, int d, char *varName) {
    int i, vaIndex, size;
    for(i = 0; i < memory->varCount; i++) {
        if(strcmp(varName, memory->varArray[i]->varName) == 0) {
            vaIndex = i;
            break;
        }
    }

    size = sizeof(double) * memory->varArray[vaIndex]->numVectors;
    memcpy(stats, memory->varArray[vaIndex]->compStats[vaIndex]->compRatio, size);
    memcpy(stats + memory->varArray[vaIndex]->numVectors, memory->varArray[vaIndex]->compStats[vaIndex]->compTime, size);
    memcpy(stats + memory->varArray[vaIndex]->numVectors * 2, memory->varArray[vaIndex]->compStats[vaIndex]->decompTime, size);
}

void sz_printVarInfo(char *varName) {
    int i, vaIndex;
    //determine index in varArray based on varName
    for(i = 0; i < memory->varCount; i++) {
        if(strcmp(varName, memory->varArray[i]->varName) == 0) {
            vaIndex = i;
            break;
        }
    }
    if(vaIndex == -99) {
        printf("Error printing variable info: varName '%s' not found!", varName);
        exit(0);
    }
 
    printf("Variable: %s\n", memory->varArray[vaIndex]->varName);
    printf("\tnumVectors: %ld\n", memory->varArray[vaIndex]->numVectors);
    printf("\tdimensions: %ld,%ld,%ld,%ld,%ld\n", memory->varArray[vaIndex]->dimensions[0],
        memory->varArray[vaIndex]->dimensions[1], memory->varArray[vaIndex]->dimensions[2],
        memory->varArray[vaIndex]->dimensions[3], memory->varArray[vaIndex]->dimensions[4]);
 
    for(int i = 0; i < memory->varArray[vaIndex]->numVectors; i++) {
        if(memory->varArray[vaIndex]->dataVectors[i] != NULL) {
            printf("vector[%d]: compRatio = %.6lf, compTime = %.6lf, decompTime = %.6lf\n", i,
                memory->varArray[vaIndex]->compStats[vaIndex]->compRatio[i],
                memory->varArray[vaIndex]->compStats[vaIndex]->compTime[i],
                memory->varArray[vaIndex]->compStats[vaIndex]->decompTime[i]);
        }
    }
    printf("\n");
}
 
/*  
    cfgFile is the sz.config file used for sz
    numVars is the number of individual variables needed
*/
void sz_constructor(char *cfgFile, int numVars) {
    memory = malloc(sizeof(compMem));   //malloc global memory
    memory->maxNumVars = numVars;
    memory->varCount = 0;
 
    //malloc array of variable ptrs
    memory->varArray = malloc(sizeof(*memory->varArray) * numVars);
    for(int i = 0; i < numVars; i++) {
        memory->varArray[i] = malloc(sizeof(variable));
    }   
 
    //sz
    printf("cfgFile=%s\n", cfgFile);
    int status = SZ_Init(cfgFile);
    if(status == SZ_NSCS)
        exit(0);
}
 
/*
    responsible for freeing all resources malloc-ed by C code
    as well as any compressor cleanup 
*/
void sz_destructor(void) {
    //"memory" cleanup
    for(int i = 0; i < memory->varCount; i++) {
        for(int j = 0; j < memory->varArray[i]->numVectors; j++) {
            if(memory->varArray[i]->dataVectors[j] != NULL) {
                free(memory->varArray[i]->dataVectors[j]);
            }
        }

        free(memory->varArray[i]->dataVectors);
        free(memory->varArray[i]->numBytes);
        free(memory->varArray[i]->regVarBuffer);

        for(int s = 0; s < memory->maxNumVars; s++) {
            free(memory->varArray[i]->compStats[s]->compRatio);
            free(memory->varArray[i]->compStats[s]->compTime);
            free(memory->varArray[i]->compStats[s]->decompTime);
            free(memory->varArray[i]->compStats[s]);
        }
        free(memory->varArray[i]->compStats);
    }

    for(int k = 0; k < memory->maxNumVars; k++) {
        free(memory->varArray[k]);
    }
     
    //sz cleanup
    free(memory->varArray);
    free(memory);
    SZ_Finalize();

    //zfp cleanup
    for(int z = 0; z < ZFP_NUM_STREAM; z++) {
        if (field[z] != NULL)
            zfp_field_free(field[z]);
        if (zfp[z] != NULL)
            zfp_stream_close(zfp[z]);
    }
    
}
 
/*
    varName is variable string to register
    numVectors is the number of vectors (M) for the variable being setup
    d5-d1 are the dimensions for the variable, start with d1->d5
*/
void sz_registerVar(char *varName, size_t numVectors, int d5,  int d4, int d3, int d2, int d1) {
    int i;
    //TODO: bound check and range check
    //set up new variable
    strcpy(memory->varArray[memory->varCount]->varName, varName);   //set varName
    memory->varArray[memory->varCount]->numVectors = numVectors;    //set numVectors (M)
    memory->varArray[memory->varCount]->dimensions[0] = d1;         //set dimensions
    memory->varArray[memory->varCount]->dimensions[1] = d2;
    memory->varArray[memory->varCount]->dimensions[2] = d3;
    memory->varArray[memory->varCount]->dimensions[3] = d4;
    memory->varArray[memory->varCount]->dimensions[4] = d5;
    memory->varArray[memory->varCount]->numBytes = malloc(sizeof(size_t) * numVectors);
    memory->varArray[memory->varCount]->dataVectors = malloc(sizeof(unsigned char *) * numVectors);
    
    //malloc space for statistics
    memory->varArray[memory->varCount]->compStats = 
        malloc(sizeof(*memory->varArray[memory->varCount]->compStats) * memory->maxNumVars);
    for(i = 0; i < memory->maxNumVars; i++) {
        memory->varArray[memory->varCount]->compStats[i] = malloc(sizeof(stats));
        memory->varArray[memory->varCount]->compStats[i]->compRatio = malloc(sizeof(double) * numVectors);
        memory->varArray[memory->varCount]->compStats[i]->compTime = malloc(sizeof(double) * numVectors);
        memory->varArray[memory->varCount]->compStats[i]->decompTime = malloc(sizeof(double) * numVectors);
    }
 
    //init dataVectors ptrs to NULL and numBytes or each dataVector to 0
    for(i = 0; i < numVectors; i++) {
        memory->varArray[memory->varCount]->dataVectors[i] = NULL;
        memory->varArray[memory->varCount]->numBytes[i] = 0;
    }
 
    //determine dimensions for malloc of regVarBuffer
    size_t dim = 1;
    for(i = 0; i < 5; i++) {
        if(memory->varArray[memory->varCount]->dimensions[i] > 0)
            dim = dim * memory->varArray[memory->varCount]->dimensions[i];
    }
 
    memory->varArray[memory->varCount]->regVarBuffer = malloc(sizeof(double) * dim);
 
    //Temporal only here
    //SZ_registerVar(memory->varCount+1, varName, SZ_DOUBLE, memory->varArray[memory->varCount]->regVarBuffer,
      //  ABS, 1e-6, 1e-6, 1e-6, d5, d4, d3, d2, d1);

    memory->varCount++;
}
 
/*
    data is data to be compressed
    size is required w/ SWIG, contains the number of elements in data
    varName is the variable string
    dvIndex is the desired storage location of the compressed data within its dataVectors array
    compType 0=std, 1=temporal, 2=truncation, 3=zfp
*/
void sz_comp(double *data, int size, char *varName, int dvIndex, int errBoundMode, double errBound, int compType) {
    int i, vaIndex = -99;
    size_t outSize; //num of compressed bytes out
    unsigned char *bytes = NULL;
 
    //determine index in varArray based on varName and error check
    for(i = 0; i < memory->varCount; i++) {
        if(strcmp(varName, memory->varArray[i]->varName) == 0) {
            vaIndex = i;
            break;
        }
    }
    if(vaIndex == -99) {
        printf("Error: varName '%s' not found!", varName);
        exit(0);
    }
    if(dvIndex >= memory->varArray[i]->numVectors) {
        printf("Error: index '%d' is not within range: 0-%ld\n", dvIndex, (memory->varArray[i]->numVectors)-1);
        exit(0);
    }
 
    //std sz compression
    if(compType == 0) {
        switch(errBoundMode) {
            case 0:
                cost_start();
                bytes = SZ_compress_args(SZ_DOUBLE, data, &outSize, ABS, errBound, errBound, errBound, 
                    memory->varArray[vaIndex]->dimensions[4], memory->varArray[vaIndex]->dimensions[3], 
                    memory->varArray[vaIndex]->dimensions[2], memory->varArray[vaIndex]->dimensions[1], 
                    memory->varArray[vaIndex]->dimensions[0]);
                cost_end();
                break;
            case 1:
                cost_start();
                bytes = SZ_compress_args(SZ_DOUBLE, data, &outSize, REL, errBound, errBound, errBound, 
                    memory->varArray[vaIndex]->dimensions[4], memory->varArray[vaIndex]->dimensions[3], 
                    memory->varArray[vaIndex]->dimensions[2], memory->varArray[vaIndex]->dimensions[1], 
                    memory->varArray[vaIndex]->dimensions[0]);
                cost_end();
                break;
            case 2:
                cost_start();
                bytes = SZ_compress_args(SZ_DOUBLE, data, &outSize, PW_REL, errBound, errBound, errBound, 
                    memory->varArray[vaIndex]->dimensions[4], memory->varArray[vaIndex]->dimensions[3], 
                    memory->varArray[vaIndex]->dimensions[2], memory->varArray[vaIndex]->dimensions[1], 
                    memory->varArray[vaIndex]->dimensions[0]);
                cost_end();
                break;
            default:
                printf("Error: errBoundMode '%d' is undefined\n", errBoundMode);
                exit(0);
        }
    } else if(compType == 1) {  //temporal sz compression
        unsigned char var_ids[3] = {1,2,3};
        memcpy(memory->varArray[i]->regVarBuffer, data, size * sizeof(double));
        cost_start();
        SZ_compress_ts_select_var(SZ_PERIO_TEMPORAL_COMPRESSION, var_ids, 1, &bytes, &outSize); //vaIndex+1
        cost_end();
    } else if(compType == 2) {  //truncation, double -> float
        double *buf = malloc(sizeof(double) * size);
        memcpy(buf, data, sizeof(double) * size);
        bytes = malloc(sizeof(double) * size);

        cost_start();
        for(int j = 0; j < size; j++) {
            //buf[j] = (double)... other data types
            buf[j] = (double)((float)data[j]);
        }

        memcpy(bytes, buf, sizeof(double) * size);
        cost_end();
        outSize = (sizeof(double) * size) / 2;
    } else if(compType == 3) { //zfp
        size_t bufsize;    //byte size of compressed buffer
        size_t zfpsize;    //byte size of compressed stream
        memcpy(oArray, data, sizeof(double) * DATA_LEN);
        //field[dvIndex] = zfp_field_1d(oArray, type, DATA_LEN);    //for 1d data
        field[dvIndex] =  zfp_field_3d(oArray, type, 9, 73, 399);   //"boost" w/ 3d fake
        zfp[dvIndex] = zfp_stream_open(NULL);
        zfp_stream_set_accuracy(zfp[dvIndex], errBound);
        bufsize = zfp_stream_maximum_size(zfp[dvIndex], field[dvIndex]);

        if(buffer[dvIndex] == NULL)
            buffer[dvIndex] = malloc(bufsize);
        if (stream[dvIndex] == NULL)
            stream[dvIndex] = stream_open(buffer[dvIndex], bufsize);
        zfp_stream_set_bit_stream(zfp[dvIndex], stream[dvIndex]);

        cost_start();
        zfp_stream_rewind(zfp[dvIndex]);
        zfpsize = zfp_compress(zfp[dvIndex], field[dvIndex]);
        cost_end();
        //printf("zfpsize: %ld\n", zfpsize);

        if(!zfpsize)
            fprintf(stderr, "compression failed\n");
        outSize = zfpsize;
    }

    //save statistics
    //printf("compRatio[%d] = %lf\n", dvIndex, (double)((sizeof(double)*size)/outSize));
    memory->varArray[vaIndex]->compStats[vaIndex]->compRatio[dvIndex] = (double)((sizeof(double)*size)/(double)outSize);
    memory->varArray[vaIndex]->compStats[vaIndex]->compTime[dvIndex] = totalCost;

    if(compType != 3) { //zfp data is saved within zfp globals for simplicity of testing
        //save pointer to return compressed bytes
        if(memory->varArray[vaIndex]->dataVectors[dvIndex] == NULL) {
            memory->varArray[vaIndex]->dataVectors[dvIndex] = bytes;
        } else {
            free(memory->varArray[vaIndex]->dataVectors[dvIndex]);
            memory->varArray[vaIndex]->dataVectors[dvIndex] = bytes;
        }
    }

    //save # of compressed bytes (outSize) for decompression
    memory->varArray[vaIndex]->numBytes[dvIndex] = outSize;
}
 
/*
    decompData is pointer to decompressed (out) data
    out is the # of elements in the output data
    varName is the variable to decompress
    dvIndex is the desired index of the variable to decompress
    compType 0=std, 1=temporal, 2=truncation, 3=zfp
*/
void sz_decomp(double *decompData, int out, char *varName, int dvIndex, int compType) {
    int i, vaIndex = -99;
    double *data = NULL;
 
    //determine index in varArray based on varName and error check
    for(i = 0; i < memory->varCount; i++) {
        if(strcmp(varName, memory->varArray[i]->varName) == 0) {
            vaIndex = i;
            break;
        }
    }
    if(vaIndex == -99) {
        printf("Error: varName '%s' not found!", varName);
        exit(0);
    }
    if(dvIndex >= memory->varArray[i]->numVectors) {
        printf("Error: index '%d' is not within range: 0-%ld\n", dvIndex, memory->varArray[i]->numVectors-1);
        exit(0);
    }
 
    if(compType == 0) {
        cost_start();
        data = SZ_decompress(SZ_DOUBLE, memory->varArray[i]->dataVectors[dvIndex], 
            memory->varArray[i]->numBytes[dvIndex], 
            memory->varArray[vaIndex]->dimensions[4], memory->varArray[vaIndex]->dimensions[3], 
            memory->varArray[vaIndex]->dimensions[2], memory->varArray[vaIndex]->dimensions[1], 
            memory->varArray[vaIndex]->dimensions[0]);
        memcpy(decompData, data, sizeof(double) * out);
        free(data);
        cost_end();
    } else if(compType == 1) {
        cost_start();
        unsigned char var_ids[3] = {1,2,3};
        memset(memory->varArray[vaIndex]->regVarBuffer, 0, sizeof(double) * 262143);
        SZ_decompress_ts_select_var(var_ids, vaIndex+1, memory->varArray[i]->dataVectors[dvIndex], 
            memory->varArray[vaIndex]->numBytes[dvIndex]);
        memcpy(decompData, memory->varArray[vaIndex]->regVarBuffer, sizeof(double) * out);
        cost_end();
    } else if(compType == 2) {
        cost_start();
        memcpy(decompData, memory->varArray[i]->dataVectors[dvIndex], sizeof(double) * out);
        cost_end();
    } else if(compType == 3) {
        int status = 0;

        cost_start();
        zfp_stream_rewind(zfp[dvIndex]);
        status = zfp_decompress(zfp[dvIndex], field[dvIndex]);

        if(!status)
            fprintf(stderr, "decompression failed\n");
        memcpy(decompData, oArray, sizeof(double) * DATA_LEN);
        cost_end();
    }

    memory->varArray[vaIndex]->compStats[vaIndex]->decompTime[dvIndex] = totalCost;
}