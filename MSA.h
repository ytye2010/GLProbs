#ifndef _MSA_H
#define _MSA_H
#include "MSADef.h"
#include "MSAGuideTree.h"

#include "SafeVector.h"
#include "MultiSequence.h"
#include "ScoreType.h"
#include "ProbabilisticModel.h"
#include "SparseMatrix.h"
#include <string>

using namespace std;

class MSAGuideTree;
struct TreeNode;
class MSA {
public:
	MSA(int argc, char* argv[]);
	~MSA();

	static void getSysTime(double * dtime);
	MSAGuideTree* getGuideTree() {
		return tree;
	}
	int * getSeqsWeights() {
		return seqsWeights;
	}
private:
	//print heading
	void PrintHeading();
	//print usage
	void printUsage();
	//do multiple sequence alignment
	void doAlign();

	//for sequence weights
	void createSeqsWeights(int seqsNum);
	void releaseSeqsWeights();

	//weights of sequences
	int * seqsWeights;
	//guide tree
	MSAGuideTree* tree;
	//output file
	string alignOutFileName;
	std::ostream* alignOutFile;
private:
	SafeVector<string> ParseParams(int argc, char *argv[]);
	void PrintParameters(const char *message, const VF &initDistrib,
			const VF &gapOpen, const VF &gapExtend, const VVF &emitPairs,
			const VF &emitSingle, const char *filename);

	SafeVector<string> PostProbsParseParams(int argc, char **argv);
	MultiSequence *doAlign(MultiSequence *sequence,
			const ProbabilisticModel &model, int levelid);
	void ReadParameters();
	MultiSequence* ProcessTree(TreeNode *tree, MultiSequence *sequences,
			const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
			const ProbabilisticModel &model);
	MultiSequence *ComputeFinalAlignment(MSAGuideTree *tree,
			MultiSequence *sequences,
			const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
			const ProbabilisticModel &model,int levelid);
	MultiSequence *AlignAlignments(MultiSequence *align1, MultiSequence *align2,
			const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
			const ProbabilisticModel &model);
	SafeVector<SafeVector<SparseMatrix *> > DoRelaxation(float* seqsWeights,
			MultiSequence *sequences,
			SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices);
	SafeVector<SafeVector<SparseMatrix *> > DoRelaxation( MultiSequence *sequences,
			SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices);

	void Relax(float weight, SparseMatrix *matXZ, SparseMatrix *matZY,VF &posterior);//weight 
	void Relax1(float weight, SparseMatrix *matXZ, SparseMatrix *matZY,VF &posterior);//weight 
	void Relax(SparseMatrix *matXZ, SparseMatrix *matZY,VF &posterior);//unweight 
	void Relax1(SparseMatrix *matXZ, SparseMatrix *matZY,VF &posterior);//unweight 

	int DoIterativeRefinement(
			const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
			const ProbabilisticModel &model, MultiSequence* &alignment);
	void DoIterativeRefinementTreeNode(
			const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
			const ProbabilisticModel &model, MultiSequence* &alignment,
			int nodeIndex);
	void WriteAnnotation(MultiSequence *alignment,
			const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices);
	int ComputeScore(const SafeVector<pair<int, int> > &active,
			const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices);
    int AdjustmentTest( MultiSequence *sequences,const ProbabilisticModel &model );//Determine the Model
#ifdef _OPENMP
	//private struct
	struct SeqsPair {
		int seq1;
		int seq2;
	};
	int numPairs;
	SeqsPair* seqsPairs;
#endif
};

#endif
