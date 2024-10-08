/***********************************************
 * # GLProbs: Aligning multiple sequences adaptively
 * #
 * # Contact: YE Yongtao, Department of Computer Science,
 * #			 The University of Hong Kong.
 * # Emails:	 ytye@cs.hku.hk
 * #
 * # Main routines for GLProbs program (May 2013).
 * #
 * ************************************************/

#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <set>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <iomanip>

#include <sys/time.h>
#include <time.h>

#include "MSA.h"
#include "MSAClusterTree.h"
#include "Defaults.h"

#ifdef _OPENMP
#include <omp.h>
#endif

string parametersInputFilename = "";
string parametersOutputFilename = "no training";
string annotationFilename = "";

bool enableVerbose = false;
bool enableAnnotation = false;
bool enableClustalWOutput = false;
bool enableAlignOrder = false;
int numConsistencyReps = 2;
int numPreTrainingReps = 0;
int numIterativeRefinementReps = 100;

float cutoff = 0;

///////////////////////////////
// local pair-HMM and double affine pair-HMM variables
//////////////////////////////
VF initDistrib(NumMatrixTypes);
VF gapOpen(2 * NumInsertStates);
VF gapExtend(2 * NumInsertStates);
VVF emitPairs(256, VF(256, 1e-10));//subsititution matrix for double affine pair-HMM
VF emitSingle(256, 1e-5);//subsititution matrix for double affine pair-HMM

string alphabet = alphabetDefault;

const int MIN_PRETRAINING_REPS = 0;
const int MAX_PRETRAINING_REPS = 20;
const int MIN_CONSISTENCY_REPS = 0;
const int MAX_CONSISTENCY_REPS = 5;
const int MIN_ITERATIVE_REFINEMENT_REPS = 0;
const int MAX_ITERATIVE_REFINEMENT_REPS = 1000;

string posteriorProbsFilename = "";
bool allscores = true;
string infilename;

///////////////////////////////
// global pair-HMM variables
//////////////////////////////
int flag_gui = 0;   //0: no gui related o/p 
//1: gui related o/p generated
int flag_ppscore = 0; //0: no pp score sequence added to o/p fasta alignment
//1: pp score seq added to o/p fasta alignment

float g_gap_open1, g_gap_open2, g_gap_ext1, g_gap_ext2;
char *aminos, *bases, matrixtype[20] = "gonnet_160";
int subst_index[26];
 
double sub_matrix[26][26];      //subsititution matrix for global pair-HMM
double normalized_matrix[26][26];// be used to compute sequences' similarity score
int firstread = 0;		//this makes sure that matrices are read only once 

float TEMPERATURE = 5;
int MATRIXTYPE = 160;
int prot_nuc = 0;		//0=prot, 1=nucleotide

float GAPOPEN = 0;
float GAPEXT = 0;
int numThreads = 0;

double startTime = 0;
double timeUsed = 0;

//argument support
typedef struct {
	char input[30];
	int matrix;
	int N;
	float T;
	float beta;
	char opt;			//can be 'P' or 'M'
	float gapopen;
	float gapext;
} argument_decl;

argument_decl argument;

extern inline void read_sustitution_matrix(char *fileName);
extern void setmatrixtype(int le);
extern inline int matrixtype_to_int();
extern inline void read_dna_matrix();
extern inline void read_vtml_la_matrix();
extern void init_arguments();

unsigned long int GetTime()
{
    struct timeval tv;
    gettimeofday ( &tv, NULL );
    return tv.tv_usec + ( tv.tv_sec * 1000000L );
}

double GetElapsedTime ( unsigned long int startTime )
{
    return ( GetTime() - startTime ) / 1000000.0L;
}

MSA::MSA(int argc, char* argv[]) {

	PrintHeading();

	//parse program parameters
	SafeVector<string> sequenceNames = ParseParams(argc, argv);

	//initialize arguments for partition function
	init_arguments();

	ReadParameters();

	//read the input sequences
	MultiSequence *sequences = new MultiSequence();
	assert(sequences);
	for (int i = 0; i < (int) sequenceNames.size(); i++) {
		cerr << "Loading sequence file: " << sequenceNames[i] << endl;
		sequences->LoadMFA(sequenceNames[i], true);
	}
	//allocate space for sequence weights
	this->seqsWeights = new int[sequences->GetNumSequences()];
	//initilaize parameters for OPENMP
#ifdef _OPENMP
	if(numThreads <= 0) {
		numThreads = omp_get_num_procs();
	}

	//set OpenMP to use dynamic number of threads which is equal to the number of processor cores on the host
	omp_set_num_threads(numThreads);
#endif
    /**************************************
	// average percent identity 
    // Divergent(<=25%) levelid = 0
	// Medium(25%-40%) levelid = 1
	// Similar(40%-70%) levelid = 2
	// High Similar(>70%) levelid = 3
    ***************************************/
	
	startTime = GetTime();
	int levelid = AdjustmentTest(sequences,ProbabilisticModel(initDistrib, gapOpen, gapExtend, emitPairs,emitSingle));
	timeUsed = GetElapsedTime ( startTime );
 	cerr << "[Main] Model Determination used " << fixed << setprecision(4) << timeUsed << " seconds." << endl;
	// now, we can perform the alignments and write them out
	MultiSequence *alignment = doAlign(sequences,
			ProbabilisticModel(initDistrib, gapOpen, gapExtend, emitPairs,emitSingle), levelid);

	//write the alignment results to standard output
	if (enableClustalWOutput) {
		alignment->WriteALN(*alignOutFile);
	} else {
		alignment->WriteMFA(*alignOutFile);
	}

	//release resources
	delete alignment;
	delete[] this->seqsWeights;
	
	delete sequences;
}

MSA::~MSA() {
	/*close the output file*/
	if (alignOutFileName.length() > 0) {
		((std::ofstream*) alignOutFile)->close();
	}
}



/////////////////////////////////////////////////////////////////
// PrintParameters()
//
// Prints MSAPROBS parameters to STDERR.  If a filename is
// specified, then the parameters are also written to the file.
/////////////////////////////////////////////////////////////////

void MSA::PrintParameters(const char *message, const VF &initDistrib,
		const VF &gapOpen, const VF &gapExtend, const VVF &emitPairs,
		const VF &emitSingle, const char *filename) {

	// print parameters to the screen
	cerr << message << endl << "    initDistrib[] = { ";
	for (int i = 0; i < NumMatrixTypes; i++)
		cerr << setprecision(10) << initDistrib[i] << " ";
	cerr << "}" << endl << "        gapOpen[] = { ";
	for (int i = 0; i < NumInsertStates * 2; i++)
		cerr << setprecision(10) << gapOpen[i] << " ";
	cerr << "}" << endl << "      gapExtend[] = { ";
	for (int i = 0; i < NumInsertStates * 2; i++)
		cerr << setprecision(10) << gapExtend[i] << " ";
	cerr << "}" << endl << endl;

	// if a file name is specified
	if (filename) {

		// attempt to open the file for writing
		FILE *file = fopen(filename, "w");
		if (!file) {
			cerr << "ERROR: Unable to write parameter file: " << filename
					<< endl;
			exit(1);
		}

		// if successful, then write the parameters to the file
		for (int i = 0; i < NumMatrixTypes; i++)
			fprintf(file, "%.10f ", initDistrib[i]);
		fprintf(file, "\n");
		for (int i = 0; i < 2 * NumInsertStates; i++)
			fprintf(file, "%.10f ", gapOpen[i]);
		fprintf(file, "\n");
		for (int i = 0; i < 2 * NumInsertStates; i++)
			fprintf(file, "%.10f ", gapExtend[i]);
		fprintf(file, "\n");
		fprintf(file, "%s\n", alphabet.c_str());
		for (int i = 0; i < (int) alphabet.size(); i++) {
			for (int j = 0; j <= i; j++)
				fprintf(file, "%.10f ",
						emitPairs[(unsigned char) alphabet[i]][(unsigned char) alphabet[j]]);
			fprintf(file, "\n");
		}
		for (int i = 0; i < (int) alphabet.size(); i++)
			fprintf(file, "%.10f ", emitSingle[(unsigned char) alphabet[i]]);
		fprintf(file, "\n");
		fclose(file);
	}
}

/////////////////////////////////////////////////////////////////
// doAlign()
//
// First computes all pairwise posterior probability matrices.
// Then, computes new parameters if training, or a final
// alignment, otherwise.
/////////////////////////////////////////////////////////////////

extern VF *ComputePostProbs(int a, int b, string seq1, string seq2);
MultiSequence* MSA::doAlign(MultiSequence *sequences,
		const ProbabilisticModel &model, int levelid) {
	assert(sequences);

	startTime = GetTime();
	//get the number of sequences
	const int numSeqs = sequences->GetNumSequences();
	//create distance matrix
	VVF distances(numSeqs, VF(numSeqs, 0));
        //creat sparseMatrices
        SafeVector<SafeVector<SparseMatrix *> > sparseMatrices(numSeqs,
			SafeVector<SparseMatrix *>(numSeqs, NULL)); 

#ifdef _OPENMP
	//calculate sequence pairs for openmp model
	int pairIdx = 0;
	numPairs = (numSeqs - 1) * numSeqs / 2;
	seqsPairs = new SeqsPair[numPairs];
	for(int a = 0; a < numSeqs; a++) {
		for(int b = a + 1; b < numSeqs; b++) {
			seqsPairs[pairIdx].seq1 = a;
			seqsPairs[pairIdx].seq2 = b;
			pairIdx++;
		}
	}
#endif
	// do all pairwise alignments for posterior probability matrices
#ifdef _OPENMP
#pragma omp parallel for private(pairIdx) default(shared) schedule(dynamic)
	for(pairIdx = 0; pairIdx < numPairs; pairIdx++) {
		int a= seqsPairs[pairIdx].seq1;
		int b = seqsPairs[pairIdx].seq2;
		if(enableVerbose) {
#pragma omp critical
			cerr <<"tid "<<omp_get_thread_num()<<" a "<<a<<" b "<<b<<endl;
		}
#else
	for (int a = 0; a < numSeqs - 1; a++) {
		for (int b = a + 1; b < numSeqs; b++) {
#endif
			Sequence *seq1 = sequences->GetSequence(a);
			Sequence *seq2 = sequences->GetSequence(b);

			//posterior probability matrix
			VF* posterior;

			//medium similarity use local pair-HMM
			if(levelid == 1){
				// compute forward and backward probabilities
				VF *forward = model.ComputeForwardMatrix(seq1, seq2,false);
				assert(forward);
				VF *backward = model.ComputeBackwardMatrix(seq1, seq2,false);
				assert(backward);
				// compute posterior probability 
				posterior = model.ComputePosteriorMatrix(seq1, seq2, *forward,*backward, false);
   				delete forward;
				delete backward; 

			}
			//high similarity use global pair-HMM
			else if(levelid >= 2) posterior = ::ComputePostProbs(a, b, seq1->GetString(),seq2->GetString());

			//divergent use combined model
			else{

				//double affine pair-HMM
				// compute forward and backward probabilities
				VF *forward = model.ComputeForwardMatrix(seq1, seq2);
				assert(forward);
				VF *backward = model.ComputeBackwardMatrix(seq1, seq2);
				assert(backward);
				// compute posterior probability 
				VF *double_posterior = model.ComputePosteriorMatrix(seq1, seq2, *forward,*backward);
				assert(double_posterior);		
   				delete forward;
				delete backward;   
                 
				//global pair-HMM
				// compute posterior probability 
				VF *global_posterior = ::ComputePostProbs(a, b, seq1->GetString(),seq2->GetString());
				assert(global_posterior);
				//local pair-HMM
				// compute forward and backward probabilities
				forward = model.ComputeForwardMatrix(seq1, seq2,false);
				assert(forward);
				backward = model.ComputeBackwardMatrix(seq1, seq2,false);
				assert(backward);
				// compute posterior probability 
				posterior = model.ComputePosteriorMatrix(seq1, seq2, *forward,*backward, false);
				assert(posterior);
   				delete forward;
				delete backward; 				
				//combined model
				//merge probalign + local + probcons 
				VF::iterator ptr1 = double_posterior->begin();			
				VF::iterator ptr2 = global_posterior->begin();
				VF::iterator ptr = posterior->begin();                 
				for (int i = 0; i <= seq1->GetLength(); i++) {
					for (int j = 0; j <= seq2->GetLength(); j++) {
						float v1 = *ptr1;
						float v2 = *ptr2;
						float v3 = *ptr;
						*ptr = sqrt((v1*v1 + v2*v2 + v3*v3)/3);
						ptr1++;
						ptr2++;
						ptr++;
					}
				}
                delete double_posterior;
				delete global_posterior;
			}

            assert(posterior);
			// perform the pairwise sequence alignment
			pair<SafeVector<char> *, float> alignment = model.ComputeAlignment(
			seq1->GetLength(), seq2->GetLength(), *posterior);

			//compute expected accuracy
			distances[a][b] = distances[b][a] = 1.0f - alignment.second
					/ min(seq1->GetLength(), seq2->GetLength());

			// compute sparse representations
			sparseMatrices[a][b] = new SparseMatrix(seq1->GetLength(),
			seq2->GetLength(), *posterior);
			sparseMatrices[b][a] = NULL;

			delete posterior;
			delete alignment.first;
#ifndef _OPENMP
		}
#endif
	} 
	
	timeUsed = GetElapsedTime ( startTime );	
 	cerr << "[Main] HMM computation used " << fixed << setprecision(4) << timeUsed << " seconds." << endl;
	double lastUsed = timeUsed;

	//create the guide tree
	this->tree = new MSAClusterTree(this, distances, numSeqs);
	this->tree->create();

	timeUsed = GetElapsedTime ( startTime );
 	cerr << "[Main] Guide Tree construction used " << timeUsed - lastUsed << " seconds." << endl;
	lastUsed = timeUsed;

	// perform the consistency transformation the desired number of times
	float* fweights = new float[numSeqs];
	for (int r = 0; r < numSeqs; r++) {
		fweights[r] = ((float) seqsWeights[r]) / INT_MULTIPLY;
		fweights[r] *= 10;
	}
	for (int r = 0; r < numConsistencyReps; r++) {
		SafeVector<SafeVector<SparseMatrix *> > newSparseMatrices =
				DoRelaxation(fweights, sequences, sparseMatrices);

		// now replace the old posterior matrices
		for (int i = 0; i < numSeqs; i++) {
			for (int j = 0; j < numSeqs; j++) {
				delete sparseMatrices[i][j];
				sparseMatrices[i][j] = newSparseMatrices[i][j];
			}
		}
	}
	delete[] fweights;
#ifdef _OPENMP
	delete [] seqsPairs;
#endif

	timeUsed = GetElapsedTime ( startTime );
 	cerr << "[Main] Consistence transformation used " << timeUsed - lastUsed << " seconds." << endl;

	//compute the final multiple sequence alignment
	MultiSequence *finalAlignment = ComputeFinalAlignment(this->tree, sequences,
			sparseMatrices, model,levelid);

	// build annotation
	if (enableAnnotation) {
		WriteAnnotation(finalAlignment, sparseMatrices);
	}
	//destroy the guide tree
	delete this->tree;
	this->tree = 0;

	// delete sparse matrices
	for (int a = 0; a < numSeqs - 1; a++) {
		for (int b = a + 1; b < numSeqs; b++) {
			delete sparseMatrices[a][b];
			delete sparseMatrices[b][a];
		}
	}

	return finalAlignment;
}

/////////////////////////////////////////////////////////////////
// GetInteger()
//
// Attempts to parse an integer from the character string given.
// Returns true only if no parsing error occurs.
/////////////////////////////////////////////////////////////////

bool GetInteger(char *data, int *val) {
	char *endPtr;
	long int retVal;

	assert(val);

	errno = 0;
	retVal = strtol(data, &endPtr, 0);
	if (retVal == 0 && (errno != 0 || data == endPtr))
		return false;
	if (errno != 0 && (retVal == LONG_MAX || retVal == LONG_MIN))
		return false;
	if (retVal < (long) INT_MIN || retVal > (long) INT_MAX)
		return false;
	*val = (int) retVal;
	return true;
}

/////////////////////////////////////////////////////////////////
// GetFloat()
//
// Attempts to parse a float from the character string given.
// Returns true only if no parsing error occurs.
/////////////////////////////////////////////////////////////////

bool GetFloat(char *data, float *val) {
	char *endPtr;
	double retVal;

	assert(val);

	errno = 0;
	retVal = strtod(data, &endPtr);
	if (retVal == 0 && (errno != 0 || data == endPtr))
		return false;
	if (errno != 0 && (retVal >= 1000000.0 || retVal <= -1000000.0))
		return false;
	*val = (float) retVal;
	return true;
}

/////////////////////////////////////////////////////////////////
// ReadParameters()
//
// Read initial distribution, transition, and emission
// parameters from a file.
/////////////////////////////////////////////////////////////////

void MSA::ReadParameters() {

	ifstream data;

	emitPairs = VVF(256, VF(256, 1e-10));
	emitSingle = VF(256, 1e-5);

	// read initial state distribution and transition parameters
	if (parametersInputFilename == string("")) {
		if (NumInsertStates == 1) {
			for (int i = 0; i < NumMatrixTypes; i++)
				initDistrib[i] = initDistrib1Default[i];
			for (int i = 0; i < 2 * NumInsertStates; i++)
				gapOpen[i] = gapOpen1Default[i];
			for (int i = 0; i < 2 * NumInsertStates; i++)
				gapExtend[i] = gapExtend1Default[i];
		} else if (NumInsertStates == 2) {
			for (int i = 0; i < NumMatrixTypes; i++)
				initDistrib[i] = initDistrib2Default[i];
			for (int i = 0; i < 2 * NumInsertStates; i++)
				gapOpen[i] = gapOpen2Default[i];
			for (int i = 0; i < 2 * NumInsertStates; i++)
				gapExtend[i] = gapExtend2Default[i];
		} else {
			cerr
					<< "ERROR: No default initial distribution/parameter settings exist"
					<< endl << "       for " << NumInsertStates
					<< " pairs of insert states.  Use --paramfile." << endl;
			exit(1);
		}

		alphabet = alphabetDefault;

		for (int i = 0; i < (int) alphabet.length(); i++) {
			emitSingle[(unsigned char) tolower(alphabet[i])] =
					emitSingleDefault[i];
			emitSingle[(unsigned char) toupper(alphabet[i])] =
					emitSingleDefault[i];
			for (int j = 0; j <= i; j++) {
				emitPairs[(unsigned char) tolower(alphabet[i])][(unsigned char) tolower(
						alphabet[j])] = emitPairsDefault[i][j];
				emitPairs[(unsigned char) tolower(alphabet[i])][(unsigned char) toupper(
						alphabet[j])] = emitPairsDefault[i][j];
				emitPairs[(unsigned char) toupper(alphabet[i])][(unsigned char) tolower(
						alphabet[j])] = emitPairsDefault[i][j];
				emitPairs[(unsigned char) toupper(alphabet[i])][(unsigned char) toupper(
						alphabet[j])] = emitPairsDefault[i][j];
				emitPairs[(unsigned char) tolower(alphabet[j])][(unsigned char) tolower(
						alphabet[i])] = emitPairsDefault[i][j];
				emitPairs[(unsigned char) tolower(alphabet[j])][(unsigned char) toupper(
						alphabet[i])] = emitPairsDefault[i][j];
				emitPairs[(unsigned char) toupper(alphabet[j])][(unsigned char) tolower(
						alphabet[i])] = emitPairsDefault[i][j];
				emitPairs[(unsigned char) toupper(alphabet[j])][(unsigned char) toupper(
						alphabet[i])] = emitPairsDefault[i][j];
			}
		}
	} else {
		data.open(parametersInputFilename.c_str());
		if (data.fail()) {
			cerr << "ERROR: Unable to read parameter file: "
					<< parametersInputFilename << endl;
			exit(1);
		}

		string line[3];
		for (int i = 0; i < 3; i++) {
			if (!getline(data, line[i])) {
				cerr
						<< "ERROR: Unable to read transition parameters from parameter file: "
						<< parametersInputFilename << endl;
				exit(1);
			}
		}
		istringstream data2;
		data2.clear();
		data2.str(line[0]);
		for (int i = 0; i < NumMatrixTypes; i++)
			data2 >> initDistrib[i];
		data2.clear();
		data2.str(line[1]);
		for (int i = 0; i < 2 * NumInsertStates; i++)
			data2 >> gapOpen[i];
		data2.clear();
		data2.str(line[2]);
		for (int i = 0; i < 2 * NumInsertStates; i++)
			data2 >> gapExtend[i];

		if (!getline(data, line[0])) {
			cerr << "ERROR: Unable to read alphabet from scoring matrix file: "
					<< parametersInputFilename << endl;
			exit(1);
		}

		// read alphabet as concatenation of all characters on alphabet line
		alphabet = "";
		string token;
		data2.clear();
		data2.str(line[0]);
		while (data2 >> token)
			alphabet += token;

		for (int i = 0; i < (int) alphabet.size(); i++) {
			for (int j = 0; j <= i; j++) {
				float val;
				data >> val;
				emitPairs[(unsigned char) tolower(alphabet[i])][(unsigned char) tolower(
						alphabet[j])] = val;
				emitPairs[(unsigned char) tolower(alphabet[i])][(unsigned char) toupper(
						alphabet[j])] = val;
				emitPairs[(unsigned char) toupper(alphabet[i])][(unsigned char) tolower(
						alphabet[j])] = val;
				emitPairs[(unsigned char) toupper(alphabet[i])][(unsigned char) toupper(
						alphabet[j])] = val;
				emitPairs[(unsigned char) tolower(alphabet[j])][(unsigned char) tolower(
						alphabet[i])] = val;
				emitPairs[(unsigned char) tolower(alphabet[j])][(unsigned char) toupper(
						alphabet[i])] = val;
				emitPairs[(unsigned char) toupper(alphabet[j])][(unsigned char) tolower(
						alphabet[i])] = val;
				emitPairs[(unsigned char) toupper(alphabet[j])][(unsigned char) toupper(
						alphabet[i])] = val;
			}
		}

		for (int i = 0; i < (int) alphabet.size(); i++) {
			float val;
			data >> val;
			emitSingle[(unsigned char) tolower(alphabet[i])] = val;
			emitSingle[(unsigned char) toupper(alphabet[i])] = val;
		}
		data.close();
	}
}


/////////////////////////////////////////////////////////////////
// PrintHeading()
//
// Prints heading for GLProbs program.
/////////////////////////////////////////////////////////////////

void MSA::PrintHeading()
{
	cerr
			<< "************************************************************************"
			<< endl
			<< "GLProbs is an open-source adaptive multiple sequence alignment program."
			<< endl
			<< "Written by Yongtao Ye using code from MSAProbs vsersion 0.9.7(Y.Liu et al.)."
			<< endl
			<< "This is not for commercial activities. If any comments or problems,"
			<< endl
			<< "please contact ytye@cs.hku.hk"
			<< endl
			<< "*************************************************************************"
			<< endl;
}

/////////////////////////////////////////////////////////////////
// ParseParams()
//
// Parse all command-line options.
/////////////////////////////////////////////////////////////////

void MSA::printUsage() {
	cerr
			//<< "************************************************************************"
			//<< endl
			//<< "\tGLProbs is a open-source adaptive multiple sequence alignment program."
			//<< endl
			//<< "\tWritten by Yongtao Ye using code from MSAProbs vsersion 0.9.7(Y.Liu et al.)."
			//<< endl
			//<< "\tThis is not for commercial activities. If any comments or problems, please contact"
			//<< endl
			//<< "\tYE Yongtao(ytye@cs.hku.hk)"
			//<< endl
			//<< "*************************************************************************"
			<< endl << "Usage:" << endl
			<< "       glprobs [OPTION]... [infile]..." << endl << endl
			<< "Description:" << endl
			<< "       Align sequences in multi-FASTA format" << endl << endl
			<< "       -o, --outfile <string>" << endl
			<< "              specify the output file name (STDOUT by default)"
			<< endl << "       -num_threads <integer>" << endl
			<< "              specify the number of threads used, and otherwise detect automatically"
			<< endl << "       -clustalw" << endl
			<< "              use CLUSTALW output format instead of FASTA format"
			<< endl << endl << "       -c, --consistency REPS" << endl
			<< "              use " << MIN_CONSISTENCY_REPS << " <= REPS <= "
			<< MAX_CONSISTENCY_REPS << " (default: " << numConsistencyReps
			<< ") passes of consistency transformation" << endl << endl
			<< "       -ir, --iterative-refinement REPS" << endl
			<< "              use " << MIN_ITERATIVE_REFINEMENT_REPS
			<< " <= REPS <= " << MAX_ITERATIVE_REFINEMENT_REPS << " (default: "
			<< numIterativeRefinementReps << ") passes of iterative-refinement"
			<< endl << endl << "       -v, --verbose" << endl
			<< "              report progress while aligning (default: "
			<< (enableVerbose ? "on" : "off") << ")" << endl << endl
			<< "       -annot FILENAME" << endl
			<< "              write annotation for multiple alignment to FILENAME"
			<< endl << endl << "       -a, --alignment-order" << endl
			<< "              print sequences in alignment order rather than input order (default: "
			<< (enableAlignOrder ? "on" : "off") << ")" << endl
			<< "       -version " << endl
			<< "              print out version of GLProbs " << endl << endl;
}

SafeVector<string> MSA::ParseParams(int argc, char **argv) {
	if (argc < 2) {
		printUsage();
		exit(1);
	}
	SafeVector<string> sequenceNames;
	int tempInt;
	float tempFloat;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			//help
			if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "-?")) {
				printUsage();
				exit(1);
			//output file name
			} else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--outfile")) {
				if (i < argc - 1) {
					alignOutFileName = argv[++i];	//get the file name
				} else {
					cerr << "ERROR: String expected for option " << argv[i]
							<< endl;
					exit(1);
				}
/*
			// parameter file
			} else if (!strcmp (argv[i], "-p") || !strcmp (argv[i], "--paramfile")){
           			if (i < argc - 1)
                			parametersInputFilename = string (argv[++i]);
           			else {
               				cerr << "ERROR: Filename expected for option " << argv[i] << endl;
               				exit (1);
             			}
*/
			//number of threads used
           		} else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "-num_threads")) {
				if (i < argc - 1) {
					if (!GetInteger(argv[++i], &tempInt)) {
						cerr << " ERROR: invalid integer following option "
								<< argv[i - 1] << ": " << argv[i] << endl;
						exit(1);
					} else {
						if (tempInt < 0) {
							tempInt = 0;
						}
						numThreads = tempInt;
					}
				} else {
					cerr << "ERROR: Integer expected for option " << argv[i]
							<< endl;
					exit(1);
				}
			
			// number of consistency transformations
			} else if (!strcmp(argv[i], "-c")
					|| !strcmp(argv[i], "--consistency")) {
				if (i < argc - 1) {
					if (!GetInteger(argv[++i], &tempInt)) {
						cerr << "ERROR: Invalid integer following option "
								<< argv[i - 1] << ": " << argv[i] << endl;
						exit(1);
					} else {
						if (tempInt < MIN_CONSISTENCY_REPS
								|| tempInt > MAX_CONSISTENCY_REPS) {
							cerr << "ERROR: For option " << argv[i - 1]
									<< ", integer must be between "
									<< MIN_CONSISTENCY_REPS << " and "
									<< MAX_CONSISTENCY_REPS << "." << endl;
							exit(1);
						} else {
							numConsistencyReps = tempInt;
						}
					}
				} else {
					cerr << "ERROR: Integer expected for option " << argv[i]
							<< endl;
					exit(1);
				}
			}

			// number of randomized partitioning iterative refinement passes
			else if (!strcmp(argv[i], "-ir")
					|| !strcmp(argv[i], "--iterative-refinement")) {
				if (i < argc - 1) {
					if (!GetInteger(argv[++i], &tempInt)) {
						cerr << "ERROR: Invalid integer following option "
								<< argv[i - 1] << ": " << argv[i] << endl;
						exit(1);
					} else {
						if (tempInt < MIN_ITERATIVE_REFINEMENT_REPS
								|| tempInt > MAX_ITERATIVE_REFINEMENT_REPS) {
							cerr << "ERROR: For option " << argv[i - 1]
									<< ", integer must be between "
									<< MIN_ITERATIVE_REFINEMENT_REPS << " and "
									<< MAX_ITERATIVE_REFINEMENT_REPS << "."
									<< endl;
							exit(1);
						} else
							numIterativeRefinementReps = tempInt;
					}
				} else {
					cerr << "ERROR: Integer expected for option " << argv[i]
							<< endl;
					exit(1);
				}
			}

			// annotation files
			else if (!strcmp(argv[i], "-annot")) {
				enableAnnotation = true;
				if (i < argc - 1) {
					annotationFilename = argv[++i];
				} else {
					cerr << "ERROR: FILENAME expected for option " << argv[i]
							<< endl;
					exit(1);
				}
			}

			// clustalw output format
			else if (!strcmp(argv[i], "-clustalw")) {
				enableClustalWOutput = true;
			}

			// cutoff
			else if (!strcmp(argv[i], "-co") || !strcmp(argv[i], "--cutoff")) {
				if (i < argc - 1) {
					if (!GetFloat(argv[++i], &tempFloat)) {
						cerr << "ERROR: Invalid floating-point value following option "
								<< argv[i - 1] << ": " << argv[i] << endl;
						exit(1);
					} else {
						if (tempFloat < 0 || tempFloat > 1) {
							cerr << "ERROR: For option " << argv[i - 1]
									<< ", floating-point value must be between 0 and 1."
									<< endl;
							exit(1);
						} else
							cutoff = tempFloat;
					}
				} else {
					cerr << "ERROR: Floating-point value expected for option "
							<< argv[i] << endl;
					exit(1);
				}
			}

			// verbose reporting
			else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
				enableVerbose = true;
			}

			// alignment order
			else if (!strcmp(argv[i], "-a")
					|| !strcmp(argv[i], "--alignment-order")) {
				enableAlignOrder = true;
			}

			//print out version
			else if (!strcmp(argv[i], "-version")) {
				cerr << "GLProbs version " << VERSION << endl;
				exit(1);
			}
			// bad arguments
			else {
				cerr << "ERROR: Unrecognized option: " << argv[i] << endl;
				exit(1);
			}
		} else {
			sequenceNames.push_back(string(argv[i]));
		}
	}

	/*check the output file name*/
	//cerr << "-------------------------------------" << endl;
	if (alignOutFileName.length() == 0) {
		cerr << "The final alignments will be printed out to STDOUT" << endl;
		alignOutFile = &std::cout;
	} else {
		cerr << "Open the output file " << alignOutFileName << endl;
		alignOutFile = new ofstream(alignOutFileName.c_str(),
				ios::binary | ios::out | ios::trunc);
	}
	//cerr << "-------------------------------------" << endl;
	return sequenceNames;
}

/////////////////////////////////////////////////////////////////
// ProcessTree()
//
// Process the tree recursively.  Returns the aligned sequences
// corresponding to a node or leaf of the tree.
/////////////////////////////////////////////////////////////////

MultiSequence* MSA::ProcessTree(TreeNode *tree, MultiSequence *sequences,
		const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
		const ProbabilisticModel &model) {

	MultiSequence *result;

	// check if this is a node of the alignment tree
	//if (tree->GetSequenceLabel() == -1){
	if (tree->leaf == NODE) {
		MultiSequence *alignLeft = ProcessTree(tree->left, sequences,
				sparseMatrices, model);
		MultiSequence *alignRight = ProcessTree(tree->right, sequences,
				sparseMatrices, model);

		assert(alignLeft);
		assert(alignRight);

		result = AlignAlignments(alignLeft, alignRight, sparseMatrices, model);
		assert(result);

		delete alignLeft;
		delete alignRight;
	}

	// otherwise, this is a leaf of the alignment tree
	else {
		result = new MultiSequence();
		assert(result);
		//result->AddSequence (sequences->GetSequence(tree->GetSequenceLabel())->Clone());
		result->AddSequence(sequences->GetSequence(tree->idx)->Clone());
	}

	return result;
}

/////////////////////////////////////////////////////////////////
// ComputeFinalAlignment()
//
// Compute the final alignment by calling ProcessTree(), then
// performing iterative refinement as needed.
/////////////////////////////////////////////////////////////////

MultiSequence* MSA::ComputeFinalAlignment(MSAGuideTree*tree,
		MultiSequence *sequences,
		const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
		const ProbabilisticModel &model, int levelid) {

	startTime = GetTime();
	MultiSequence *alignment = ProcessTree(tree->getRoot(), sequences,
			sparseMatrices, model);
	timeUsed = GetElapsedTime ( startTime );
 	cerr << "[Main] Profile-Profile alignment used " << timeUsed << " seconds." << endl;
	double lastUsed = timeUsed;
	
	SafeVector<int> oldOrdering;
	int numSeqs = alignment->GetNumSequences();
	if (enableAlignOrder) {
		for (int i = 0; i < numSeqs; i++)
			oldOrdering.push_back(alignment->GetSequence(i)->GetSortLabel());
		alignment->SaveOrdering();
		enableAlignOrder = false;
	}

    //DoIterativeRefinement() return 1,2: this refinement unsuccessful
	if(levelid == 3 || numSeqs >= 150 ) numIterativeRefinementReps=10;
	int ineffectiveness = 0;
	for (int i = 0; i < numIterativeRefinementReps; i++){
		int flag = DoIterativeRefinement(sparseMatrices, model, alignment);
		if(numSeqs > 25 && numSeqs < 150 && levelid < 3){
			if(flag > 0){
			    if(numIterativeRefinementReps < 4*numSeqs) 
					numIterativeRefinementReps ++;				
                if(flag == 1) ineffectiveness ++;
			}
 			//else ineffectiveness = 0;
 			if(ineffectiveness > 2*numSeqs && i>100) break;
		}		
	}
	timeUsed = GetElapsedTime ( startTime );
 	cerr << "[Main] Refinement used " << timeUsed - lastUsed << " seconds." << endl;

	return alignment;
}

/////////////////////////////////////////////////////////////////
// AlignAlignments()
//
// Returns the alignment of two MultiSequence objects.
/////////////////////////////////////////////////////////////////

MultiSequence* MSA::AlignAlignments(MultiSequence *align1,
		MultiSequence *align2,
		const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
		const ProbabilisticModel &model) {

	// print some info about the alignment
	if (enableVerbose) {
		for (int i = 0; i < align1->GetNumSequences(); i++)
			cerr << ((i == 0) ? "[" : ",")
					<< align1->GetSequence(i)->GetLabel();
		cerr << "] vs. ";
		for (int i = 0; i < align2->GetNumSequences(); i++)
			cerr << ((i == 0) ? "[" : ",")
					<< align2->GetSequence(i)->GetLabel();
		cerr << "]: ";
	}
#if 0
	VF *posterior = model.BuildPosterior (align1, align2, sparseMatrices, cutoff);
#else
	VF *posterior = model.BuildPosterior(getSeqsWeights(), align1, align2,
			sparseMatrices, cutoff);
#endif
	// compute an "accuracy" measure for the MSA before refinement

	pair<SafeVector<char> *, float> alignment;
	//perform alignment
	alignment = model.ComputeAlignment(align1->GetSequence(0)->GetLength(),
			align2->GetSequence(0)->GetLength(), *posterior);

	delete posterior;

	if (enableVerbose) {

		// compute total length of sequences
		int totLength = 0;
		for (int i = 0; i < align1->GetNumSequences(); i++)
			for (int j = 0; j < align2->GetNumSequences(); j++)
				totLength += min(align1->GetSequence(i)->GetLength(),
						align2->GetSequence(j)->GetLength());

		// give an "accuracy" measure for the alignment
		cerr << alignment.second / totLength << endl;
	}

	// now build final alignment
	MultiSequence *result = new MultiSequence();
	for (int i = 0; i < align1->GetNumSequences(); i++)
		result->AddSequence(
				align1->GetSequence(i)->AddGaps(alignment.first, 'X'));
	for (int i = 0; i < align2->GetNumSequences(); i++)
		result->AddSequence(
				align2->GetSequence(i)->AddGaps(alignment.first, 'Y'));
	if (!enableAlignOrder)
		result->SortByLabel();

	// free temporary alignment
	delete alignment.first;

	return result;
}

/////////////////////////////////////////////////////////////////
// DoRelaxation()
//
// Performs one round of the weighted probabilistic consistency transformation.
//                     
/////////////////////////////////////////////////////////////////

SafeVector<SafeVector<SparseMatrix *> > MSA::DoRelaxation(float* seqsWeights,
		MultiSequence *sequences,
		SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices) {
	const int numSeqs = sequences->GetNumSequences();

	SafeVector<SafeVector<SparseMatrix *> > newSparseMatrices(numSeqs,
			SafeVector<SparseMatrix *>(numSeqs, NULL));

	// for every pair of sequences
#ifdef _OPENMP
	int pairIdx;
#pragma omp parallel for private(pairIdx) default(shared) schedule(dynamic)
	for(pairIdx = 0; pairIdx < numPairs; pairIdx++) {
		int i = seqsPairs[pairIdx].seq1;
		int j = seqsPairs[pairIdx].seq2;
		//float wi = seqsWeights[i];
		//float wj = seqsWeights[j];
#else
	for (int i = 0; i < numSeqs; i++) {
		//float wi = seqsWeights[i];
		for (int j = i + 1; j < numSeqs; j++) {
			//float wj = seqsWeights[j];
#endif
			Sequence *seq1 = sequences->GetSequence(i);
			Sequence *seq2 = sequences->GetSequence(j);

			if (enableVerbose) {
#ifdef _OPENMP
#pragma omp critical
#endif
				cerr << "Relaxing (" << i + 1 << ") " << seq1->GetHeader()
						<< " vs. " << "(" << j + 1 << ") " << seq2->GetHeader()
						<< ": ";
			}
			// get the original posterior matrix
			VF *posteriorPtr = sparseMatrices[i][j]->GetPosterior();
			assert(posteriorPtr);
			VF &posterior = *posteriorPtr;

			const int seq1Length = seq1->GetLength();
			const int seq2Length = seq2->GetLength();

			// contribution from the summation where z = x and z = y
			//float w = wi * wi * wj + wi * wj * wj;
			//float sumW = w;
			for (int k = 0; k < (seq1Length + 1) * (seq2Length + 1); k++) {
                                //posterior[k] = w*posterior[k];
				posterior[k] += posterior[k];
			}

			if (enableVerbose)
				cerr << sparseMatrices[i][j]->GetNumCells() << " --> ";

			// contribution from all other sequences
			for (int k = 0; k < numSeqs; k++) {
				if (k != i && k != j) {
					//float wk = seqsWeights[k];
					//float w = wi * wj * wk;
					//sumW += w;
					if (k < i)
						Relax1(sparseMatrices[k][i], sparseMatrices[k][j],posterior);
					else if (k > i && k < j)
						Relax(sparseMatrices[i][k], sparseMatrices[k][j],posterior);
					else {
						SparseMatrix *temp = sparseMatrices[j][k]->ComputeTranspose();
						Relax(sparseMatrices[i][k], temp, posterior);
						delete temp;
					}
				}
			}
			//cerr<<"sumW "<<sumW<<endl;
			for (int k = 0; k < (seq1Length + 1) * (seq2Length + 1); k++) {
				//posterior[k] /= sumW;
				posterior[k] /= numSeqs;
			}
			// mask out positions not originally in the posterior matrix
			SparseMatrix *matXY = sparseMatrices[i][j];
			for (int y = 0; y <= seq2Length; y++)
				posterior[y] = 0;
			for (int x = 1; x <= seq1Length; x++) {
				SafeVector<PIF>::iterator XYptr = matXY->GetRowPtr(x);
				SafeVector<PIF>::iterator XYend = XYptr + matXY->GetRowSize(x);
				VF::iterator base = posterior.begin() + x * (seq2Length + 1);
				int curr = 0;
				while (XYptr != XYend) {

					// zero out all cells until the first filled column
					while (curr < XYptr->first) {
						base[curr] = 0;
						curr++;
					}

					// now, skip over this column
					curr++;
					++XYptr;
				}

				// zero out cells after last column
				while (curr <= seq2Length) {
					base[curr] = 0;
					curr++;
				}
			}

			// save the new posterior matrix
			newSparseMatrices[i][j] = new SparseMatrix(seq1->GetLength(),
					seq2->GetLength(), posterior);
			newSparseMatrices[j][i] = NULL;

			if (enableVerbose)
				cerr << newSparseMatrices[i][j]->GetNumCells() << " -- ";

			delete posteriorPtr;

			if (enableVerbose)
				cerr << "done." << endl;
#ifndef _OPENMP
		}
#endif
	}

	return newSparseMatrices;
}

/////////////////////////////////////////////////////////////////
// Relax()
//
// Computes the consistency transformation for a single sequence
// z, and adds the transformed matrix to "posterior".
/////////////////////////////////////////////////////////////////

//with weight
void MSA::Relax(float weight, SparseMatrix *matXZ, SparseMatrix *matZY,
		VF &posterior) {

	assert(matXZ);
	assert(matZY);

	int lengthX = matXZ->GetSeq1Length();
	int lengthY = matZY->GetSeq2Length();
	assert(matXZ->GetSeq2Length() == matZY->GetSeq1Length());

	// for every x[i]
	for (int i = 1; i <= lengthX; i++) {
		SafeVector<PIF>::iterator XZptr = matXZ->GetRowPtr(i);
		SafeVector<PIF>::iterator XZend = XZptr + matXZ->GetRowSize(i);

		VF::iterator base = posterior.begin() + i * (lengthY + 1);

		// iterate through all x[i]-z[k]
		while (XZptr != XZend) {
			SafeVector<PIF>::iterator ZYptr = matZY->GetRowPtr(XZptr->first);
			SafeVector<PIF>::iterator ZYend = ZYptr
					+ matZY->GetRowSize(XZptr->first);
			const float XZval = XZptr->second;

			// iterate through all z[k]-y[j]
			while (ZYptr != ZYend) {
				base[ZYptr->first] += weight * XZval * ZYptr->second;
				ZYptr++;
			}
			XZptr++;
		}
	}
}

//without weight
void MSA::Relax(SparseMatrix *matXZ, SparseMatrix *matZY,
		VF &posterior) {

	assert(matXZ);
	assert(matZY);

	int lengthX = matXZ->GetSeq1Length();
	int lengthY = matZY->GetSeq2Length();
	assert(matXZ->GetSeq2Length() == matZY->GetSeq1Length());

	// for every x[i]
	for (int i = 1; i <= lengthX; i++) {
		SafeVector<PIF>::iterator XZptr = matXZ->GetRowPtr(i);
		SafeVector<PIF>::iterator XZend = XZptr + matXZ->GetRowSize(i);

		VF::iterator base = posterior.begin() + i * (lengthY + 1);

		// iterate through all x[i]-z[k]
		while (XZptr != XZend) {
			SafeVector<PIF>::iterator ZYptr = matZY->GetRowPtr(XZptr->first);
			SafeVector<PIF>::iterator ZYend = ZYptr
					+ matZY->GetRowSize(XZptr->first);
			const float XZval = XZptr->second;

			// iterate through all z[k]-y[j]
			while (ZYptr != ZYend) {
                                base[ZYptr->first] += XZval * ZYptr->second;
				ZYptr++;
			}
			XZptr++;
		}
	}
}

/////////////////////////////////////////////////////////////////
// Relax1()
//
// Computes the consistency transformation for a single sequence
// z, and adds the transformed matrix to "posterior".
/////////////////////////////////////////////////////////////////

//with weight
void MSA::Relax1(float weight, SparseMatrix *matZX, SparseMatrix *matZY,VF &posterior) {

	assert(matZX);
	assert(matZY);

	int lengthZ = matZX->GetSeq1Length();
	int lengthY = matZY->GetSeq2Length();

	// for every z[k]
	for (int k = 1; k <= lengthZ; k++) {
		SafeVector<PIF>::iterator ZXptr = matZX->GetRowPtr(k);
		SafeVector<PIF>::iterator ZXend = ZXptr + matZX->GetRowSize(k);

		// iterate through all z[k]-x[i]
		while (ZXptr != ZXend) {
			SafeVector<PIF>::iterator ZYptr = matZY->GetRowPtr(k);
			SafeVector<PIF>::iterator ZYend = ZYptr + matZY->GetRowSize(k);
			const float ZXval = ZXptr->second;
			VF::iterator base = posterior.begin()
					+ ZXptr->first * (lengthY + 1);

			// iterate through all z[k]-y[j]
			while (ZYptr != ZYend) {
				base[ZYptr->first] += weight * ZXval * ZYptr->second;
				ZYptr++;
			}
			ZXptr++;
		}
	}
}

//without weight
void MSA::Relax1(SparseMatrix *matZX, SparseMatrix *matZY,VF &posterior) {

	assert(matZX);
	assert(matZY);

	int lengthZ = matZX->GetSeq1Length();
	int lengthY = matZY->GetSeq2Length();

	// for every z[k]
	for (int k = 1; k <= lengthZ; k++) {
		SafeVector<PIF>::iterator ZXptr = matZX->GetRowPtr(k);
		SafeVector<PIF>::iterator ZXend = ZXptr + matZX->GetRowSize(k);

		// iterate through all z[k]-x[i]
		while (ZXptr != ZXend) {
			SafeVector<PIF>::iterator ZYptr = matZY->GetRowPtr(k);
			SafeVector<PIF>::iterator ZYend = ZYptr + matZY->GetRowSize(k);
			const float ZXval = ZXptr->second;
			VF::iterator base = posterior.begin()
					+ ZXptr->first * (lengthY + 1);

			// iterate through all z[k]-y[j]
			while (ZYptr != ZYend) {
				base[ZYptr->first] += ZXval * ZYptr->second;
				ZYptr++;
			}
			ZXptr++;
		}
	}
}

/////////////////////////////////////////////////////////////////
// DoIterativeRefinement()
//
// Performs a single round of randomized partionining iterative
// refinement.
// return 0: successful refinement, 1: ineffective refinement, 2: fail to partion two profiles
/////////////////////////////////////////////////////////////////

int MSA::DoIterativeRefinement(
		const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
		const ProbabilisticModel &model, MultiSequence* &alignment) {
	set<int> groupOne, groupTwo;
	int numSeqs = alignment->GetNumSequences();
	int i;
	// create two separate groups
	for (i = 0; i < numSeqs; i++) {
                int index = rand();
		if (index % 2) {
			groupOne.insert(i);
		} else {
			groupTwo.insert(i);
		}
	}
	if (groupOne.empty() || groupTwo.empty()) return 2;

	// project into the two groups
	MultiSequence *groupOneSeqs = alignment->Project(groupOne);
	assert(groupOneSeqs);
	MultiSequence *groupTwoSeqs = alignment->Project(groupTwo);
	assert(groupTwoSeqs);	

//no weight profile-profile for refinement
#if 1
	VF *posterior = model.BuildPosterior (groupOneSeqs, groupTwoSeqs, sparseMatrices, cutoff);
#else
	VF *posterior = model.BuildPosterior(getSeqsWeights(), groupOneSeqs, groupTwoSeqs,
			sparseMatrices, cutoff);
#endif
	// compute an "accuracy" for the currrent MSA before refinement        
        SafeVector<SafeVector<char>::iterator> oldOnePtrs(groupOne.size());
	SafeVector<SafeVector<char>::iterator> oldTwoPtrs(groupTwo.size());
        i=0; 
	for (set<int>::const_iterator iter = groupOne.begin();
			iter != groupOne.end(); ++iter) {
		oldOnePtrs[i++] = alignment->GetSequence(*iter)->GetDataPtr();
	}
        i=0;
	for (set<int>::const_iterator iter = groupTwo.begin();
			iter != groupTwo.end(); ++iter) {
		oldTwoPtrs[i++] = alignment->GetSequence(*iter)->GetDataPtr();
	}

        VF &posteriorArr = *posterior;
        int oldLength = alignment->GetSequence(0)->GetLength();
	int groupOneindex=0; int groupTwoindex=0;
	float accuracy_before = 0; 
        int j;
	for (i = 1; i <= oldLength; i++) {
		// check to see if there is a gap in every sequence of the set
		bool foundOne = false;
		for (j = 0; !foundOne && j < (int) groupOne.size(); j++)
			foundOne = (oldOnePtrs[j][i] != '-');
		// if not, then this column counts towards the sequence length
		if (foundOne) groupOneindex ++;
		bool foundTwo = false;
		for (j = 0; !foundTwo && j < (int) groupTwo.size(); j++)
			foundTwo = (oldTwoPtrs[j][i] != '-');
		if (foundTwo) groupTwoindex ++;
                if(foundOne && foundTwo) accuracy_before += 
				posteriorArr[groupOneindex * (groupTwoSeqs->GetSequence(0)->GetLength() + 1) + groupTwoindex];
	}
       
	pair<SafeVector<char> *, float> refinealignment;
	//perform alignment
	refinealignment = model.ComputeAlignment(groupOneSeqs->GetSequence(0)->GetLength(),
			groupTwoSeqs->GetSequence(0)->GetLength(), *posterior);
        delete posterior;
	// now build final alignment
	MultiSequence *result = new MultiSequence();
	for (int i = 0; i < groupOneSeqs->GetNumSequences(); i++)
		result->AddSequence(
			groupOneSeqs->GetSequence(i)->AddGaps(refinealignment.first, 'X'));
	for (int i = 0; i < groupTwoSeqs->GetNumSequences(); i++)
		result->AddSequence(
			groupTwoSeqs->GetSequence(i)->AddGaps(refinealignment.first, 'Y'));
	// free temporary alignment
	delete refinealignment.first;
	delete alignment;
        alignment = result;
	delete groupOneSeqs;
	delete groupTwoSeqs;
        if(accuracy_before == refinealignment.second) return 1;
        else return 0;	
}


void MSA::DoIterativeRefinementTreeNode(
		const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices,
		const ProbabilisticModel &model, MultiSequence* &alignment,
		int nodeIndex) {
	set<int> groupOne, groupTwo;
	int numSeqs = alignment->GetNumSequences();

	vector<bool> inGroup1;
	inGroup1.resize(numSeqs);
	for (int i = 0; i < numSeqs; i++) {
		inGroup1[i] = false;
	}

	AlignmentOrder* orders = this->tree->getAlignOrders();
	AlignmentOrder* order = &orders[nodeIndex];
	for (int i = 0; i < order->leftNum; i++) {
		int si = order->leftLeafs[i];
		inGroup1[si] = true;
	}
	for (int i = 0; i < order->rightNum; i++) {
		int si = order->rightLeafs[i];
		inGroup1[si] = true;
	}
	// create two separate groups
	for (int i = 0; i < numSeqs; i++) {
		if (inGroup1[i]) {
			groupOne.insert(i);
		} else {
			groupTwo.insert(i);
		}
	}
	if (groupOne.empty() || groupTwo.empty())
		return;

	// project into the two groups
	MultiSequence *groupOneSeqs = alignment->Project(groupOne);
	assert(groupOneSeqs);
	MultiSequence *groupTwoSeqs = alignment->Project(groupTwo);
	assert(groupTwoSeqs);
	delete alignment;

	// realign
	alignment = AlignAlignments(groupOneSeqs, groupTwoSeqs, sparseMatrices,
			model);

	delete groupOneSeqs;
	delete groupTwoSeqs;
}

/////////////////////////////////////////////////////////////////
// WriteAnnotation()
//
// Computes annotation for multiple alignment and write values
// to a file.
/////////////////////////////////////////////////////////////////

void MSA::WriteAnnotation(MultiSequence *alignment,
		const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices) {
	ofstream outfile(annotationFilename.c_str());

	if (outfile.fail()) {
		cerr << "ERROR: Unable to write annotation file." << endl;
		exit(1);
	}

	const int alignLength = alignment->GetSequence(0)->GetLength();
	const int numSeqs = alignment->GetNumSequences();

	SafeVector<int> position(numSeqs, 0);
	SafeVector<SafeVector<char>::iterator> seqs(numSeqs);
	for (int i = 0; i < numSeqs; i++)
		seqs[i] = alignment->GetSequence(i)->GetDataPtr();
	SafeVector<pair<int, int> > active;
	active.reserve(numSeqs);

	SafeVector<int> lab;
	for (int i = 0; i < numSeqs; i++)
		lab.push_back(alignment->GetSequence(i)->GetSortLabel());

	// for every column
	for (int i = 1; i <= alignLength; i++) {

		// find all aligned residues in this particular column
		active.clear();
		for (int j = 0; j < numSeqs; j++) {
			if (seqs[j][i] != '-') {
				active.push_back(make_pair(lab[j], ++position[j]));
			}
		}

		sort(active.begin(), active.end());
		outfile << setw(4) << ComputeScore(active, sparseMatrices) << endl;
	}

	outfile.close();
}

/////////////////////////////////////////////////////////////////
// ComputeScore()
//
// Computes the annotation score for a particular column.
/////////////////////////////////////////////////////////////////

int MSA::ComputeScore(const SafeVector<pair<int, int> > &active,
		const SafeVector<SafeVector<SparseMatrix *> > &sparseMatrices) {

	if (active.size() <= 1)
		return 0;

	// ALTERNATIVE #1: Compute the average alignment score.

	float val = 0;
	for (int i = 0; i < (int) active.size(); i++) {
		for (int j = i + 1; j < (int) active.size(); j++) {
			val += sparseMatrices[active[i].first][active[j].first]->GetValue(
					active[i].second, active[j].second);
		}
	}

	return (int) (200 * val / ((int) active.size() * ((int) active.size() - 1)));

}

/////////////////////////////////////////////////////////////////
// AdjustmentTest ()
//
// Computes the average percent identity for a particular family.
// Divergent  (<=25%) return 0
// Medium   (25%-40%) return 1
// Similar  (40%-70%) return 2
// High Similar(>70%) return 3
/////////////////////////////////////////////////////////////////

int MSA::AdjustmentTest(MultiSequence *sequences,const ProbabilisticModel &model){
	assert(sequences);

	//get the number of sequences
	const int numSeqs = sequences->GetNumSequences();
    //average identity for all sequences
    float identity = 0;

#ifdef _OPENMP
	//calculate sequence pairs for openmp model
	int pairIdx = 0;
	numPairs = (numSeqs - 1) * numSeqs / 2;
	seqsPairs = new SeqsPair[numPairs];
	for(int a = 0; a < numSeqs; a++) {
		for(int b = a + 1; b < numSeqs; b++) {
			seqsPairs[pairIdx].seq1 = a;
			seqsPairs[pairIdx].seq2 = b;
			pairIdx++;
		}
	}
#endif
    
	// do all pairwise alignments for family similarity 
	float* PIDs = new float[ (numSeqs - 1) * numSeqs / 2 * sizeof(float) ];//store percent identity of every pair sequences

#ifdef _OPENMP
#pragma omp parallel for private(pairIdx) default(shared) schedule(dynamic)
	for(pairIdx = 0; pairIdx < numPairs; pairIdx++) {
		int a= seqsPairs[pairIdx].seq1;
		int b = seqsPairs[pairIdx].seq2;
		if(enableVerbose) {
#pragma omp critical
			cerr <<"tid "<<omp_get_thread_num()<<" a "<<a<<" b "<<b<<endl;
		}
#else
	int pairIdx = -1;	
	for (int a = 0; a < numSeqs - 1; a++) {
		for (int b = a + 1; b < numSeqs; b++) {
			pairIdx++;
#endif
			Sequence *seq1 = sequences->GetSequence(a);
			Sequence *seq2 = sequences->GetSequence(b);
			pair<SafeVector<char> *, float> alignment = model.ComputeViterbiAlignment(seq1,seq2);
			SafeVector<char>::iterator iter1 = seq1->GetDataPtr();
    		SafeVector<char>::iterator iter2 = seq2->GetDataPtr();

            float N_correct_match = 0;
			//float N_alignment = 0;
            int i = 1;int j = 1;
			for (SafeVector<char>::iterator iter = alignment.first->begin(); 
				iter != alignment.first->end(); ++iter){
				//N_alignment += 1;
				if (*iter == 'B'){
					unsigned char c1 = (unsigned char) iter1[i++];
					unsigned char c2 = (unsigned char) iter2[j++];
   					if(c1==c2) N_correct_match += 1;
				}
                else if(*iter == 'X') i++;
 				else if(*iter == 'Y') j++;
            }
            if(i!= seq1->GetLength()+1 || j!= seq2->GetLength() + 1 ) cerr << "percent identity error"<< endl;
            PIDs[pairIdx] = N_correct_match / alignment.first->size();
            identity += N_correct_match / alignment.first->size();
			//identity += N_correct_match / alignment.first->size();
			delete alignment.first;                   
#ifndef _OPENMP
		}
#endif
	} 
	identity /= numPairs;//average percent identity

	//compute the variance
	float variance = 0;
	for (int k = 0; k < (numSeqs-1)*numSeqs/2; k++) variance += (PIDs[k]-identity)*(PIDs[k]-identity);
	variance /= (numSeqs-1)*numSeqs/2;
	variance = sqrt(variance);
	
    //adjust the parameter of leaving RX/RY in random pair-HMM      
    if( identity <= 0.15 ) initDistrib[2] = 0.143854;
	else if( identity <= 0.2 ) initDistrib[2] = 0.191948;
	else if( identity <= 0.25 ) initDistrib[2] = 0.170705;
	else if( identity <= 0.3 ) initDistrib[2] = 0.100675;
	else if( identity <= 0.35 ) initDistrib[2] = 0.090755;
	else if( identity <= 0.4 ) initDistrib[2] = 0.146188;
    else if( identity <= 0.45 ) initDistrib[2] = 0.167858;
	else if( identity <= 0.5) initDistrib[2] = 0.250769;

    if( identity <= 0.25 ) return 0;
    else if( identity <= 0.4) return 1;
	else if( identity <= 0.7) return 2;
    else return 3;
}
