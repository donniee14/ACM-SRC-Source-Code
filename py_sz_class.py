import sz_class
import numpy as np

class py_sz_class:
    #constructor
    def __init__(self, szCfg, errBoundMode, compType, errBound=1e-5, numVars=10):
        #print("constructor called!")
        #if self.init = 0
        self.dim = [0,0,0,0,0];
        self.errBoundMode = errBoundMode;
        #self.errBound = errBound;
        self.compType = compType;
        self.szCfg = szCfg;
        self.numVectors = 0;
        sz_class.sz_constructor(szCfg, numVars);
        #self.init = 1
 
    #destructor
    def __del__(self):
        sz_class.sz_destructor();
 
    #numVectors is M
    def registerVar(self, varName, shape, dtype=np.float64, numVectors=100):
        self.numVectors = numVectors;
        d1 = shape[0]; d2 = 0; d3 = 0; d4 = 0; d5 = 0;
        l = len(shape);

        if l == 2:
            d2 = shape[1];
        if l == 3:
            d2 = shape[1]; d3 = shape[2];
        if l == 4:
            d2 = shape[1]; d3 = shape[2]; d4 = shape[3];
        if l == 5:
            d2 = shape[1]; d3 = shape[3]; d4 = shape[3]; d2 = shape[4];

        #print(d1,d2,d3,d4,d5);
        self.dim=(d1,d2,d3,d4,d5);
        sz_class.sz_registerVar(varName, numVectors, d5, d4, d3, d2, d1);
 
    def compress(self, data, varName, index, errBoundMode=0, errBound=1e-5, compType=0):
        #decide which errBoundMode to use: 0=ABS, 1=REL, 2=PW_REL...
        #last arg of sz_comp is compType, 0=std sz, 1=temporal sz, 2=truncation, 3=zfp
        data = np.ravel(data);
        #print(len(data))
        sz_class.sz_comp(data, varName, index, 0, errBound, compType);
 
    def decompress(self, varName, index, compType=0):
        #must use dimensions for output array size and reshaping
        if self.dim[1] == 0:
            data = sz_class.sz_decomp(self.dim[0], varName, index, compType);
        elif self.dim[2] == 0:
            data = sz_class.sz_decomp(self.dim[0]*self.dim[1], varName, index, compType);
            data = np.reshape((self.dim[0], self.dim[1]));
        elif self.dim[3] == 0:
            data = sz_class.sz_decomp(self.dim[0]*self.dim[1]*self.dim[2], varName, index, compType);
            data = np.reshape((self.dim[0], self.dim[1], self.dim[2]));
        elif self.dim[4] == 0:
            data = sz_class.sz_decomp(self.dim[0]*self.dim[1]*self.dim[2]*self.dim[3], varName, index, compType);
            data = np.reshape((self.dim[0], self.dim[1], self.dim[2], self.dim[3]));
        else:
            data = sz_class.sz_decomp(self.dim[0]*self.dim[1]*self.dim[2]*self.dim[3]*self.dim[4], varName, index, compType);
            data = np.reshape((self.dim[0], self.dim[1], self.dim[2], self.dim[3], self.dim[4]));

        return data;
 
    def printStats(self, varName):
        print(" ")  #for readability
        sz_class.sz_printVarInfo(varName);

    def getStats(self, varName):
        #stats print in sets of 3: compRatio, compTime, decompTime for each vector in M
        stats = sz_class.sz_getStats(self.numVectors * 3, varName);
        return stats;


#declare global instance of memory
memory=py_sz_class("sz.config", "ABS", "STD");