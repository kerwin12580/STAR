#include "SoloFeature.h"
#include "streamFuns.h"
#include "TimeFunctions.h"
#include "serviceFuns.cpp"

#define def_MarkNoColor  (uint32) -1

void collapseUMIwith1MMlowHalf(uint32 *rGU, uint32 umiArrayStride, uint32 umiMaskLow, uint32 nU0, uint32 &nU1, uint32 &nU2, uint32 &nC, vector<array<uint32,2>> &vC)
{
    const uint32 bitTop=1<<31;
    const uint32 bitTopMask=~bitTop;

    for (uint32 iu=0; iu<umiArrayStride*nU0; iu+=umiArrayStride) {//each UMI
        uint32 iuu=iu+umiArrayStride;
        for (; iuu<umiArrayStride*nU0; iuu+=umiArrayStride) {//compare to all UMIs down

            uint32 uuXor=rGU[iu] ^ rGU[iuu];

            if ( uuXor > umiMaskLow)
                break; //upper half is different

            if (uuXor >> (__builtin_ctz(uuXor)/2)*2 > 3) //shift by even number of trailing zeros
                continue;//>1MM

            //1MM UMI

            //graph coloring
            if ( rGU[iu+2] == def_MarkNoColor && rGU[iuu+2] == def_MarkNoColor ) {//no color
                //new color
                rGU[iu+2] = nC;
                rGU[iuu+2] = nC;
                ++nC;
                nU1 -= 2;//subtract the duplicated UMIs
            } else if ( rGU[iu+2] == def_MarkNoColor ) {
                rGU[iu+2] = rGU[iuu+2];
                --nU1;//subtract the duplicated UMIs
            } else if ( rGU[iuu+2] == def_MarkNoColor ) {
                rGU[iuu+2] = rGU[iu+2];
                --nU1;//subtract the duplicated UMIs
            } else {//both color
                if (rGU[iuu+2] != rGU[iu+2]) {//color conflict
                    //uint32 p[2]={rGU[iu+2],rGU[iuu+2]};
                    vC.push_back({rGU[iu+2],rGU[iuu+2]});
                    //vC.push_back({rGU[iuu+2],rGU[iu+2]});
                };
            };

            //directional collapse
            if ( (rGU[iuu+1] & bitTop) == 0 && (rGU[iu+1] & bitTopMask)>(2*(rGU[iuu+1] & bitTopMask)-1) ) {//iuu is duplicate of iu
                rGU[iuu+1] |= bitTop;
                --nU2;//subtract the duplicated UMIs
            } else if ( (rGU[iu+1] & bitTop) == 0 && (rGU[iuu+1] & bitTopMask)>(2*(rGU[iu+1] & bitTopMask)-1) ) {//iu is duplicate of iuu
                rGU[iu+1] |= bitTop;
                --nU2;//subtract the duplicated UMIs
            };
        };
    };
};

void graphDepthFirstSearch(uint32 n, vector<vector<uint32>> &nodeEdges, vector <uint32> &nodeColor) {
    for (const auto &nn : nodeEdges[n]) {
        if (nodeColor[nn]==(uint32)-1) {//node not visited
            nodeColor[nn]=nodeColor[n];
            graphDepthFirstSearch(nn,nodeEdges,nodeColor);
        };
    };
};

uint32 graphNumberOfConnectedComponents(uint32 N, vector<array<uint32,2>> V, vector<uint32> &nodeColor) {//find number of connected components
    //N=number of nodes
    //V=edges, list of connected nodes, each pair of nodes listed once
    //simple recursive DFS

    //sort
//     qsort(V.data(),V.size(),2*sizeof(uint32),funCompareNumbers<uint32>);
    if (V.size()==0)
        return N;

    vector<vector<uint32>> nodeEdges (N);
    for (uint32 ii=0; ii<V.size(); ii++) {
        nodeEdges[V[ii][0]].push_back(V[ii][1]);
        nodeEdges[V[ii][1]].push_back(V[ii][0]);
    };

    nodeColor.resize(N,(uint32)-1); //new color (connected component) for each node (each original color)
    
    uint32 nConnComp=0;
    for (uint32 ii=0; ii<N; ii++) {
        //if (V[ii].size()==0) {//this node is not connected, no need to check. Save time beacuse this happens often
        //wrong: should be
        if (nodeEdges[ii].size()==0) {//this node is not connected, no need to check. Save time beacuse this happens often
            ++nConnComp;
            continue;
        };
        if (nodeColor[ii]==(uint32)-1) {//node not visited
            ++nConnComp;
            nodeColor[ii]=ii;
            graphDepthFirstSearch(ii,nodeEdges,nodeColor);
        };
    };
    return nConnComp;
};

void SoloFeature::collapseUMI(uint32 *rGU, uint32 rN, uint32 &nGenes, uint32 &nUtot, uint32 *umiArray) {
    
    qsort(rGU,rN,rGUarrayStride*sizeof(uint32),funCompareNumbers<uint32>); //sort by gene number

    //compact reads per gene
    uint32 gid1=-1;//current gID
    nGenes=0; //number of genes
    uint32 *gID = new uint32[min(Trans.nGe,rN)+1]; //gene IDS
    uint32 *gReadS = new uint32[min(Trans.nGe,rN)+1]; //start of gene reads TODO: allocate this array in the 2nd half of rGU
    for (uint32 iR=0; iR<rN*rGUarrayStride; iR+=rGUarrayStride) {
        if (rGU[iR]!=gid1) {//record gene boundary
            gReadS[nGenes]=iR;
            gid1=rGU[iR];
            gID[nGenes]=gid1;
            ++nGenes;
        };
        //rGU[iR]=rGU[iR+1]; //shift UMIs
        //rGU[iR+1] storage this will be used later for counting
    };
    gReadS[nGenes]=rGUarrayStride*rN;//so that gReadS[nGenes]-gReadS[nGenes-1] is the number of reads for nGenes, see below in qsort

    uint32 *nUg = new uint32[nGenes*3];//3 types of counts
    nUtot=0;
    for (uint32 iG=0; iG<nGenes; iG++) {//collapse UMIs for each gene
        uint32 *rGU1=rGU+gReadS[iG];

//         qsort(rGU1+1, (gReadS[iG+1]-gReadS[iG])/rGUarrayStride, rGUarrayStride*sizeof(uint32), funCompareNumbers<uint32>);
        //        +1 to point to UMIs
        qsort(rGU1, (gReadS[iG+1]-gReadS[iG])/rGUarrayStride, rGUarrayStride*sizeof(uint32), funCompareTypeShift<uint32,1>);
        
        //exact collapse
        uint32 iR1=-umiArrayStride; //number of distinct UMIs for this gene
        uint32 u1=-1;
        for (uint32 iR=1; iR<gReadS[iG+1]-gReadS[iG]; iR+=rGUarrayStride) {//count and collapse identical UMIs
            if (rGU1[iR]!=u1) {
                iR1 += umiArrayStride;
                u1=rGU1[iR];
                umiArray[iR1]=u1;
                umiArray[iR1+1]=0;
                umiArray[iR1+2]=def_MarkNoColor; //marks no color for graph
            };
            umiArray[iR1+1]++;
            //if ( umiArray[iR1+1]>nRumiMax) nRumiMax=umiArray[iR1+1];
        };
        uint32 nU0=(iR1+umiArrayStride)/umiArrayStride;

        //collapse with 1MM
        uint32 nU1=nU0, nU2=nU0;//2 types of 1MM collapsing
        uint32 graphN=0; //number of nodes
        vector<array<uint32,2>> graphConn;//node connections

        collapseUMIwith1MMlowHalf(umiArray, umiArrayStride, pSolo.umiMaskLow, nU0, nU1, nU2, graphN, graphConn);

        //exchange low and high half of UMIs, re-sort, and look for 1MM again
        for (uint32 iu=0; iu<umiArrayStride*nU0; iu+=umiArrayStride) {
            uint32 high=umiArray[iu]>>(pSolo.umiL);
            umiArray[iu] &= pSolo.umiMaskLow; //remove high
            umiArray[iu] <<= (pSolo.umiL); //move low to high
            umiArray[iu] |= high; //add high
        };
        qsort(umiArray, nU0, umiArrayStride*sizeof(uint32), funCompareNumbers<uint32>);
        collapseUMIwith1MMlowHalf(umiArray, umiArrayStride, pSolo.umiMaskLow, nU0, nU1, nU2, graphN, graphConn);

        nUg[3*iG]=nU0;
        vector<uint32> graphComponents;//for each node (color) - connected component number
        nUg[3*iG+1]=nU1+graphNumberOfConnectedComponents(graphN, graphConn,graphComponents);
        nUg[3*iG+2]=nU2;
        nUtot+=nUg[3*iG+1];
    };

    uint32 *rGUp=rGU;//first place where rGU is overwritten with gene counts. Before it's intact though sorted differently.
    for (uint32 iG=0; iG<nGenes; iG++) {//output for all genes
        rGUp[0]=gID[iG];
        rGUp[1]=nUg[3*iG];
        if (nUg[3*iG]>1) {//record 2 more counts
            rGUp[2]=nUg[3*iG+1];
            rGUp[3]=nUg[3*iG+2];
            rGUp += 4;
        } else {//only one count recorded, save space
            rGUp += 2;
        };
    };
    //cout << nRumiMax << '\n';

};
