////////////////////////////////////////////////////////////////
// MSAReadMatrix.cpp
//
// Utilities for reading paramaters to global pair-HMM with partition function
/////////////////////////////////////////////////////////////////

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "MSAReadMatrix.h"

#define TRACE 0

////////////////////////////////////////////////////////////
// extern variables for scoring matrix data
////////////////////////////////////////////////////////////
extern float g_gap_open1, g_gap_open2, g_gap_ext1, g_gap_ext2;
extern char *aminos, *bases, matrixtype[20];
extern int subst_index[26];

extern double sub_matrix[26][26];
extern double normalized_matrix[26][26];

extern float TEMPERATURE;
extern int MATRIXTYPE;

extern float GAPOPEN;
extern float GAPEXT;

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

//argument support
extern argument_decl argument;

/////////////////////////////////////////////////////////
//sets substitution matrix type
////////////////////////////////////////////////////////
void setmatrixtype(int le) {
	switch (le) {
	case 160:
		strcpy(matrixtype, "gonnet_160");
		break;
	case 4:
		strcpy(matrixtype, "nuc_simple");
		break;
	default:
		strcpy(matrixtype, "CUSTOM");
		break;

	};

}

///////////////////////////////////////////////////////////////////
//sets matrix flag
///////////////////////////////////////////////////////////////////
inline int matrixtype_to_int() {

	if (!strcmp(matrixtype, "nuc_simple"))
		return 4;
	else if (!strcmp(matrixtype, "gonnet_160"))
		return 160;
	else
		return 1000;

}

/////////////////////////////////////////////////////////////////
//
// Can read any scoring matrix as long as it is defined in Matrix.h
// AND it is a lower triangular 
// AND the order of amino acids/bases is mentioned
/////////////////////////////////////////////////////////////////

inline void read_matrix(score_matrix matrx) {
	int i, j, basecount, position = 0;

	bases = (char *) matrx.monomers;

	basecount = strlen(bases);

	for (i = 0; i < basecount; i++)
		subst_index[i] = -1;

	for (i = 0; i < basecount; i++)
		subst_index[bases[i] - 'A'] = i;

	if (TRACE == 1)
		printf("\nbases read: %d\n", basecount);

	for (i = 0; i < basecount; i++)
		for (j = 0; j <= i; j++) {

			double value = exp(argument.beta * matrx.matrix[position++]);
			sub_matrix[i][j] = value;
			sub_matrix[j][i] = value;
		}

	if (TRACE)
		for (i = 0; i < basecount; i++) {
			for (j = 0; j < basecount; j++)
				printf(" %g ", sub_matrix[i][j]);
			printf("\n");
		}

}

/////////////////////////////////////////////////////////////////
// read normalized residue exchange matrix
// compute sequence similarity
/////////////////////////////////////////////////////////////////

inline void read_normalized_matrix(score_matrix matrx) {
	int i, j, basecount, position = 0;

	bases = (char *) matrx.monomers;

	basecount = strlen(bases);

	for (i = 0; i < basecount; i++)
		subst_index[i] = -1;

	for (i = 0; i < basecount; i++)
		subst_index[bases[i] - 'A'] = i;

	if (TRACE == 1)
		printf("\nbases read: %d\n", basecount);

	for (i = 0; i < basecount; i++)
		for (j = 0; j <= i; j++) {

			double value =  matrx.matrix[position++];
			normalized_matrix[i][j] = value;
			normalized_matrix[j][i] = value;
		}

	if (TRACE)
		for (i = 0; i < basecount; i++) {
			for (j = 0; j < basecount; j++)
				printf(" %g ", normalized_matrix[i][j]);
			printf("\n");
		}

}
////////////////////////////////////////////////////////////////////////////////// 
//intialize the arguments (default values)
//////////////////////////////////////////////////////////////////////////////////
void init_arguments() {
	float gap_open = 0, gap_ext = 0;
	int le;

	le = matrixtype_to_int();

	argument.N = 1;
	strcpy(argument.input, "tempin");
	argument.matrix = le;
	argument.gapopen = GAPOPEN;
	argument.gapext = GAPEXT;
	argument.T = TEMPERATURE;
	argument.beta = 1.0 / TEMPERATURE;
	argument.opt = 'P';

	if (le == 4)		//NUC OPTION :default is nuc_simple
			{
		read_matrix(nuc_simple);
		gap_open = -4;
		gap_ext = -0.25;
	}

	else if (le == 160)  //PROT option: default is gonnet_160
			{
		if (TRACE)
			printf("read matrix\n");
		read_matrix(gonnet_160);
		gap_open = -22;
		gap_ext = -1;

		//read_normalized_matrix(normalized_blosum_30); 
	} else if (le == 1000) {  //Error handling
		printf("Error: enter a valid matrix type\n");
		exit(1);
		//additional matrices can only be lower triangular
	}

	//now override the gapopen and gapext
	if (argument.gapopen != 0.0 || argument.gapext != 0.00)

	{
		gap_open = -argument.gapopen;
		gap_ext = -argument.gapext;
	}

	if (TRACE)
		printf("%f %f %f %d\n", argument.T, gap_open, gap_ext, le);

	argument.gapopen = gap_open;
	argument.gapext = gap_ext;
	argument.opt = 'P';

}
