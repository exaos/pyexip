/*==================================================================*\
|                EXIP - Embeddable EXI Processor in C                |
|--------------------------------------------------------------------|
|          This work is licensed under BSD 3-Clause License          |
|  The full license terms and conditions are located in LICENSE.txt  |
\===================================================================*/

/**
 * @file genUtils.c
 * @brief Implementation of utility functions for generating Schema-informed Grammar definitions
 * @date Nov 23, 2010
 * @author Rumen Kyusakov
 * @version 0.4
 * @par[Revision] $Id: genUtils.c 220 2012-10-18 14:36:13Z kjussakov $
 */

#include "genUtils.h"
#include "memManagement.h"
#include "grammarRules.h"
#include "eventsEXI.h"
#include "stringManipulate.h"
#include "grammars.h"
#include "ioUtil.h"
#include "initSchemaInstance.h"

#define MAX_COLLISIONS_NUMBER 50

struct collisionInfo
{
	SmallIndex leftNonTerminal;
	SmallIndex rightNonTerminal;
	SmallIndex createdNonTerminal;
};

/** Collision aware addition */
static errorCode addProductionsToARule(ProtoRuleEntry* leftRule, Index ruleIndxL, ProtoRuleEntry* rightRule,
									   struct collisionInfo* collisions, unsigned int* collisionCount, unsigned int* currRuleIndex, unsigned int initialLeftRulesCount);

// Creates the new grammar rules based on the collision information
static errorCode resolveCollisionsInGrammar(struct collisionInfo* collisions, unsigned int* collisionCount, ProtoGrammar* left, unsigned int* currRuleIndex);

/** Descending order comparison.
 * The productions are ordered with the largest event code first. */
static int compareProductions(const void* prod1, const void* prod2);

errorCode concatenateGrammars(ProtoGrammar* left, ProtoGrammar* right)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	unsigned int ruleIterL = 0;
	unsigned int ruleIterR = 0;
	unsigned int prodIterL = 0;
	unsigned int prodIterR = 0;
	struct collisionInfo collisions[MAX_COLLISIONS_NUMBER];
	unsigned int collisionCount = 0;
	unsigned int currRuleIndex;
	unsigned int initialLeftRulesCount;
	ProtoRuleEntry* pRuleEntry;

	if(left == NULL)
		return NULL_POINTER_REF;
	else if(right == NULL)
		return ERR_OK;

	/* 
	 * Concatentation works as follows:
	 * Add each rule apart from the first one on the RHS to the LHS.
	 * Example:
	 * LHS has two rules already:
	 *     left->rule[2]
	 * RHS has four rules to concatentate:
	 *     right->rule[4]
	 * Append all RHS rule productions excluding the first rule directly to LHS
	 *     left->rule[2] = right->rule[1]
	 *     left->rule[3] = right->rule[2]
	 *     left->rule[4] = right->rule[3]
	 * Merge all LHS rule productions with the first RHS rule productions ("+=" means merge)
	 *     left->rule[0] += right->rule[0]
	 *     left->rule[1] += right->rule[0]
	 * Merging occurs after the EE production; this is replaced with the first production
	 * to be merged
	 * Resolve any collisions
	 */

	/* Make a note of how many rules are currently in LHS for the new non terminal IDs */
	initialLeftRulesCount = left->count;

	/* Add in rules from the RHS */
	for(ruleIterR = 1; ruleIterR < right->count; ruleIterR++)
	{
		/* Create new rule entry in LHS proto grammar */
		tmp_err_code = addProtoRule(left, right->rule[ruleIterR].count, &pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		/* Copy the RHS productions into the new rule entry, adjusting the non terminal ID */
		for(prodIterR = 0; prodIterR < right->rule[ruleIterR].count; prodIterR++)
		{
			tmp_err_code = addProduction(pRuleEntry,
										 right->rule[ruleIterR].prod[prodIterR].eventType,
										 right->rule[ruleIterR].prod[prodIterR].typeId,
										 right->rule[ruleIterR].prod[prodIterR].qnameId,
										 right->rule[ruleIterR].prod[prodIterR].nonTermID + ((right->rule[ruleIterR].prod[prodIterR].eventType == EVENT_EE)?0:(initialLeftRulesCount-1)));
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}

	currRuleIndex = left->count;

	for(ruleIterL = 0; ruleIterL < initialLeftRulesCount; ruleIterL++)
	{
		for(prodIterL = 0; prodIterL < left->rule[ruleIterL].count; prodIterL++)
		{
			if(left->rule[ruleIterL].prod[prodIterL].eventType == EVENT_EE)
			{
				/* Remove the EE production */
				delDynEntry(&left->rule[ruleIterL].dynArray, prodIterL);
	
				/* Merge productions from RHS rule 0 into each left rule */
				tmp_err_code = addProductionsToARule(&left->rule[ruleIterL],
													 ruleIterL,
													 &right->rule[0],
													 collisions,
													 &collisionCount,
													 &currRuleIndex,
													 initialLeftRulesCount - 1);
				if(tmp_err_code != ERR_OK)
					return tmp_err_code;
				break;
			}
		}
	}

	// Create the new grammar rules based on the collision information
	return resolveCollisionsInGrammar(collisions, &collisionCount, left, &currRuleIndex);
}

static errorCode addProductionsToARule(ProtoRuleEntry* leftRule, Index ruleIndxL, ProtoRuleEntry* rightRule,
									   struct collisionInfo* collisions, unsigned int* collisionCount, unsigned int* currRuleIndex, unsigned int initialLeftRulesCount)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	unsigned int prodIterL = 0;
	unsigned int prodIterR = 0;
	unsigned char terminalCollision = FALSE;
	unsigned char collisionFound = FALSE;
	unsigned int collisIter = 0;
	Index nonTermRight;

	for(prodIterR = 0; prodIterR < rightRule->count; prodIterR++)
	{
		/* Check for terminal collisions with existing production. These must be merged */
		terminalCollision = FALSE;
		if(rightRule->prod[prodIterR].nonTermID == 0)
			nonTermRight = ruleIndxL;
		else
			nonTermRight = rightRule->prod[prodIterR].nonTermID + ((rightRule->prod[prodIterR].eventType == EVENT_EE)?0:initialLeftRulesCount);

		for(prodIterL = 0; prodIterL < leftRule->count; prodIterL++)
		{
			/* Check for the same terminal symbol e.g. SE(qname) */
			if(leftRule->prod[prodIterL].eventType == rightRule->prod[prodIterR].eventType &&
					leftRule->prod[prodIterL].typeId == rightRule->prod[prodIterR].typeId &&
					leftRule->prod[prodIterL].qnameId.uriId == rightRule->prod[prodIterR].qnameId.uriId &&
					leftRule->prod[prodIterL].qnameId.lnId == rightRule->prod[prodIterR].qnameId.lnId)
			{
				/* Now check the non-terminal ID (noting that EE's don't have a non-terminal ID) */
				collisionFound = FALSE;
				if(leftRule->prod[prodIterL].eventType == EVENT_EE ||
				   leftRule->prod[prodIterL].nonTermID == nonTermRight)
				{
					/*
					 * If the NonTerminals are the same as well, no need to add
					 * the production as it's already there
					 */
					collisionFound = TRUE;
					terminalCollision = TRUE;
					/* Check the next production in LHS... */
					break;
				}

				for(collisIter = 0; collisIter < *collisionCount; collisIter++)
				{
					if(collisions[collisIter].leftNonTerminal == leftRule->prod[prodIterL].nonTermID &&
					   collisions[collisIter].rightNonTerminal == nonTermRight)
					{
						/* Already collided nonTerminals. Modify the existing LHS non-terminal ID */
						collisionFound = TRUE;
						leftRule->prod[prodIterL].nonTermID = collisions[collisIter].createdNonTerminal;
						break;
					}

				}

				if(collisionFound == FALSE)
				{
					/* We have the same terminal but we haven't resolved the non-terminal ID yet */
					if(*collisionCount == MAX_COLLISIONS_NUMBER - 1)
						return OUT_OF_BOUND_BUFFER;

					/* Store the LHS and RHS non-terminal IDs and the current non-terminal ID for later checking */
					collisions[*collisionCount].leftNonTerminal = leftRule->prod[prodIterL].nonTermID;
					collisions[*collisionCount].rightNonTerminal = nonTermRight;
					collisions[*collisionCount].createdNonTerminal = *currRuleIndex;

					/* Modify the existing LHS non-terminal ID */
					leftRule->prod[prodIterL].nonTermID = *currRuleIndex;

					/* Increment collision array index and non-terminal ID */
					*collisionCount += 1;
					*currRuleIndex += 1;
				}

				terminalCollision = TRUE;
				break;
			}
		}

		if(terminalCollision == FALSE)
		{
			/*
			 * We have been through all LHS productions and there were no clashes
			 * so just add the production
			 */
			tmp_err_code = addProduction(leftRule,
										 rightRule->prod[prodIterR].eventType,
										 rightRule->prod[prodIterR].typeId,
										 rightRule->prod[prodIterR].qnameId,
										 nonTermRight);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}
	return ERR_OK;
}

static errorCode resolveCollisionsInGrammar(struct collisionInfo* collisions,
											unsigned int* collisionCount, ProtoGrammar* left, unsigned int* currRuleIndex)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	unsigned int collisIter = 0;
	unsigned int prodIterL = 0;
	Production* tmpProduction;
	ProtoRuleEntry* pRuleEntry;

	for(collisIter = 0; collisIter < *collisionCount; collisIter++)
	{
		tmp_err_code = addProtoRule(left, 5, &pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		for(prodIterL = 0; prodIterL < left->rule[collisions[collisIter].leftNonTerminal].count; prodIterL++)
		{
			tmpProduction = &left->rule[collisions[collisIter].leftNonTerminal].prod[prodIterL];
			tmp_err_code = addProduction(pRuleEntry,
										 tmpProduction->eventType,
										 tmpProduction->typeId,
										 tmpProduction->qnameId,
										 tmpProduction->nonTermID);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}

		tmp_err_code = addProductionsToARule(pRuleEntry,
											 left->count - 1,
											 &left->rule[collisions[collisIter].rightNonTerminal],
											 collisions,
											 collisionCount,
											 currRuleIndex,
											 0);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	return ERR_OK;
}

errorCode createSimpleTypeGrammar(Index typeId, ProtoGrammar* simpleGrammar)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	QNameID qnameID = {URI_MAX, LN_MAX};
	ProtoRuleEntry* pRuleEntry;

	tmp_err_code = createProtoGrammar(2, simpleGrammar);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(simpleGrammar, 3, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProduction(pRuleEntry, EVENT_CH, typeId, qnameID, 1);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(simpleGrammar, 2, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addEEProduction(pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	return ERR_OK;
}

errorCode createComplexTypeGrammar(ProtoGrammarArray* attrUseArray, ProtoGrammar* contentTypeGrammar,
							unsigned char isMixedContent, ProtoGrammar* complexGrammar)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	unsigned int i;

	if(isMixedContent && contentTypeGrammar != NULL)
	{
		/* If {content type} is a content model particle with mixed content, add a production for each non-terminal
		 * Content-i,j  in Content-i  as follows:
		 *  	Content-i,j  : CH [untyped value] Content-i,j
		 */
		QNameID dummyQId = {URI_MAX, LN_MAX};
		for(i = 0; i < contentTypeGrammar->count; i++)
		{
			tmp_err_code = addProduction(&contentTypeGrammar->rule[i], EVENT_CH, INDEX_MAX, dummyQId, i);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}

	if(attrUseArray->count > 0)
	{
		ProtoRuleEntry* pRuleEntry;

		tmp_err_code = createProtoGrammar(10, complexGrammar);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		tmp_err_code = addProtoRule(complexGrammar, 10, &pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		tmp_err_code = addEEProduction(pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		for(i = 0; i < attrUseArray->count; i++)
		{
			tmp_err_code = concatenateGrammars(complexGrammar, attrUseArray->pg[i]);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}

		complexGrammar->contentIndex = complexGrammar->count - 1;

		if(contentTypeGrammar != NULL)
		{
			tmp_err_code = concatenateGrammars(complexGrammar, contentTypeGrammar);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}
	else
	{
		if(contentTypeGrammar != NULL)
		{
			tmp_err_code = cloneProtoGrammar(contentTypeGrammar, complexGrammar);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
		complexGrammar->contentIndex = 0;
	}

	return ERR_OK;
}

errorCode createComplexUrTypeGrammar(ProtoGrammar* result)
{
	return NOT_IMPLEMENTED_YET;
}

errorCode createAttributeUseGrammar(unsigned char required, Index typeId,
									ProtoGrammar* attrGrammar, QNameID qnameID)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	ProtoRuleEntry* pRuleEntry;

	tmp_err_code = createProtoGrammar(2, attrGrammar);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(attrGrammar, 4, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProduction(pRuleEntry, EVENT_AT_QNAME, typeId, qnameID, 1);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	if(!required)
	{
		tmp_err_code = addEEProduction(pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	tmp_err_code = addProtoRule(attrGrammar, 4, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addEEProduction(pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	return ERR_OK;
}

errorCode createParticleGrammar(int minOccurs, int maxOccurs,
								ProtoGrammar* termGrammar, ProtoGrammar* particleGrammar)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	ProtoRuleEntry* pRuleEntry;
	int i;

	tmp_err_code = createProtoGrammar(minOccurs + 10, particleGrammar);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(particleGrammar, 5, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addEEProduction(pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	for(i = 0; i < minOccurs; i++)
	{
		tmp_err_code = concatenateGrammars(particleGrammar, termGrammar);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	if(maxOccurs - minOccurs > 0 || maxOccurs < 0) // Only if maxOccurs is unbounded or maxOccurs > minOccurs
	{
		unsigned char prodEEFound = FALSE;
		for(i = 0; i < (int)termGrammar->rule[0].count; i++)
		{
			if(termGrammar->rule[0].prod[i].eventType == EVENT_EE)
			{
				prodEEFound = TRUE;
				break;
			}
		}
		if(prodEEFound == FALSE) //	There is no production Gi,0 : EE so add one
		{
			tmp_err_code = addEEProduction(&termGrammar->rule[0]);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}

		if(maxOccurs >= 0) // {max occurs} is not unbounded
		{
			for(i = 0; i < maxOccurs - minOccurs; i++)
			{
				tmp_err_code = concatenateGrammars(particleGrammar, termGrammar);
				if(tmp_err_code != ERR_OK)
					return tmp_err_code;
			}
		}
		else // {max occurs} is unbounded
		{
			Index j = 0;
			struct collisionInfo collisions[MAX_COLLISIONS_NUMBER];
			unsigned int collisionCount = 0;
			unsigned int currRuleIndex = termGrammar->count;

			// Excluding the first rule
			for(i = 1; i < (int)termGrammar->count; i++)
			{
				for(j = 0; j < termGrammar->rule[i].count; j++)
				{
					if(termGrammar->rule[i].prod[j].eventType == EVENT_EE)
					{
						// Remove this production
						delDynEntry(&termGrammar->rule[i].dynArray, j);

						tmp_err_code = addProductionsToARule(&termGrammar->rule[i], i, &termGrammar->rule[0], collisions, &collisionCount, &currRuleIndex, 0);
						if(tmp_err_code != ERR_OK)
							return tmp_err_code;
						break;
					}
				}
			}

			// Create the new grammar rules based on the collision information
			tmp_err_code = resolveCollisionsInGrammar(collisions, &collisionCount, termGrammar, &currRuleIndex);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;

			tmp_err_code = concatenateGrammars(particleGrammar, termGrammar);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}

	return ERR_OK;
}

errorCode createElementTermGrammar(ProtoGrammar* elemGrammar, QNameID qnameID, Index grIndex)
{
	//TODO: enable support for {substitution group affiliation} property of the elements
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	ProtoRuleEntry* pRuleEntry;

	tmp_err_code = createProtoGrammar(2, elemGrammar);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(elemGrammar, 3, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProduction(pRuleEntry, EVENT_SE_QNAME, grIndex, qnameID, 1);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(elemGrammar, 3, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addEEProduction(pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	return ERR_OK;
}

errorCode createWildcardTermGrammar(String* wildcardArray, Index wildcardArraySize, UriTable* uriT, ProtoGrammar* wildcardGrammar)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	ProtoRuleEntry* pRuleEntry;
	QNameID qnameID;

	tmp_err_code = createProtoGrammar(2, wildcardGrammar);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(wildcardGrammar, wildcardArraySize + 1, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	if(wildcardArraySize == 0 ||		// default is "##any"
		(wildcardArraySize == 1 &&
				(stringEqualToAscii(wildcardArray[0], "##any") || stringEqualToAscii(wildcardArray[0], "##other"))
		)
	  )
	{
		qnameID.uriId = URI_MAX;
		qnameID.lnId = LN_MAX;
		tmp_err_code = addProduction(pRuleEntry, EVENT_SE_ALL, INDEX_MAX, qnameID, 1);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}
	else if(wildcardArraySize >= 1)
	{
		Index i;

		qnameID.lnId = LN_MAX;
		for(i = 0; i < wildcardArraySize; i++)
		{
			if(!lookupUri(uriT, wildcardArray[i], &qnameID.uriId))
			 	return UNEXPECTED_ERROR;
			tmp_err_code = addProduction(pRuleEntry, EVENT_SE_URI, INDEX_MAX, qnameID, 1);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}
	else
		return UNEXPECTED_ERROR;

	tmp_err_code = addProtoRule(wildcardGrammar, 2, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addEEProduction(pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	return ERR_OK;
}

errorCode createSequenceModelGroupsGrammar(ProtoGrammar** grArray, unsigned int arrSize, ProtoGrammar* seqGrammar)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	ProtoRuleEntry* pRuleEntry;

	if(arrSize == 0)
	{
		tmp_err_code = createProtoGrammar(3, seqGrammar);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		tmp_err_code = addProtoRule(seqGrammar, 3, &pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		tmp_err_code = addEEProduction(pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}
	else
	{
		unsigned int i;

		tmp_err_code = createProtoGrammar(10, seqGrammar);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		tmp_err_code = addProtoRule(seqGrammar, 5, &pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		tmp_err_code = addEEProduction(pRuleEntry);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		for(i = 0; i < arrSize; i++)
		{
			tmp_err_code = concatenateGrammars(seqGrammar, grArray[i]);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}
	return ERR_OK;
}

errorCode createChoiceModelGroupsGrammar(ProtoGrammarArray* pgArray, ProtoGrammar* modGrpGrammar)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	Index i;
	unsigned int ruleIterTerm = 0;
	unsigned int prodIterTerm = 0;
	struct collisionInfo collisions[MAX_COLLISIONS_NUMBER];
	unsigned int collisionCount = 0;
	unsigned int currRuleIndex;
	unsigned int initialResultRulesCount;
	ProtoGrammar* tmpGrammar;
	ProtoRuleEntry* pRuleEntry;

	tmp_err_code = createProtoGrammar(10, modGrpGrammar);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addProtoRule(modGrpGrammar, 5, &pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = addEEProduction(pRuleEntry);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmpGrammar = pgArray->pg[0];
	if(tmpGrammar == NULL)
		return NULL_POINTER_REF;

	tmp_err_code = concatenateGrammars(modGrpGrammar, tmpGrammar);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	for(i = 1; i < pgArray->count; i++)
	{

		tmpGrammar = pgArray->pg[i];

		if(tmpGrammar == NULL)
			return NULL_POINTER_REF;

		initialResultRulesCount = modGrpGrammar->count;

		for(ruleIterTerm = 1; ruleIterTerm < tmpGrammar->count; ruleIterTerm++)
		{
			tmp_err_code = addProtoRule(modGrpGrammar, 5, &pRuleEntry);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;

			for(prodIterTerm = 0; prodIterTerm < tmpGrammar->rule[ruleIterTerm].count; prodIterTerm++)
			{
				tmp_err_code = addProduction(pRuleEntry,
											 tmpGrammar->rule[ruleIterTerm].prod[prodIterTerm].eventType,
											 tmpGrammar->rule[ruleIterTerm].prod[prodIterTerm].typeId,
											 tmpGrammar->rule[ruleIterTerm].prod[prodIterTerm].qnameId,
											 tmpGrammar->rule[ruleIterTerm].prod[prodIterTerm].nonTermID + ((tmpGrammar->rule[ruleIterTerm].prod[prodIterTerm].eventType == EVENT_EE)?0:(initialResultRulesCount-1)));
				if(tmp_err_code != ERR_OK)
					return tmp_err_code;
			}
		}

		currRuleIndex = modGrpGrammar->count;

		tmp_err_code = addProductionsToARule(&modGrpGrammar->rule[0],
											 0,
											 &tmpGrammar->rule[0],
											 collisions,
											 &collisionCount,
											 &currRuleIndex,
											 initialResultRulesCount - 1);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		// Create the new grammar rules based on the collision information
		tmp_err_code = resolveCollisionsInGrammar(collisions, &collisionCount, modGrpGrammar, &currRuleIndex);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	return ERR_OK;
}

errorCode createAllModelGroupsGrammar(ProtoGrammar* pTermArray, unsigned int pTermArraySize, ProtoGrammar* modGrpGrammar)
{
	return NOT_IMPLEMENTED_YET;
}

errorCode assignCodes(ProtoGrammar* grammar)
{
	Index i = 0;

	for (i = 0; i < grammar->count; i++)
	{
		qsort(grammar->rule[i].prod, grammar->rule[i].count, sizeof(Production), compareProductions);
	}
	return ERR_OK;
}

static int compareProductions(const void* prod1, const void* prod2)
{
	Production* p1 = (Production*) prod1;
	Production* p2 = (Production*) prod2;

	if(p1->eventType < p2->eventType)
		return 1;
	else if(p1->eventType > p2->eventType)
		return -1;
	else // the same event Type
	{
		if(p1->eventType == EVENT_AT_QNAME)
		{
			return -compareQNameID(&(p1->qnameId), &(p2->qnameId));
		}
		else if(p1->eventType == EVENT_AT_URI)
		{
			if(p1->qnameId.uriId < p2->qnameId.uriId)
			{
				return 1;
			}
			else if(p1->qnameId.uriId > p2->qnameId.uriId)
			{
				return -1;
			}
			else
				return 0;
		}
		else if(p1->eventType == EVENT_SE_QNAME)
		{
			// TODO: figure out how it works??? if this really works for all cases. Seems very unpossible that it does!
			if(p1->nonTermID < p2->nonTermID)
				return 1;
			else
				return -1;
		}
		else if(p1->eventType == EVENT_SE_URI)
		{
			// TODO: figure out how it should be done
		}
		return 0;
	}
}

errorCode addEEProduction(ProtoRuleEntry* rule)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	Production *prod;
	Index prodId;

	tmp_err_code = addEmptyDynEntry(&rule->dynArray, (void**)&prod, &prodId);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	prod->eventType = EVENT_EE;
	prod->typeId = INDEX_MAX;
	prod->nonTermID = GR_VOID_NON_TERMINAL;
	prod->qnameId.uriId = URI_MAX;
	prod->qnameId.lnId = LN_MAX;

	return ERR_OK;
}

int compareQNameID(const void* qnameID1, const void* qnameID2)
{
	/**
	 *  The strings in the sting tables are sorted beforehand so simple comparison
	 *  of the indexes is enough.
	 */
	QNameID* qId1 = (QNameID*) qnameID1;
	QNameID* qId2 = (QNameID*) qnameID2;
	if(qId1->lnId < qId2->lnId)
	{
		return -1;
	}
	else if(qId1->lnId > qId2->lnId)
	{
		return 1;
	}
	else
	{
		if(qId1->uriId < qId2->uriId)
		{
			return -1;
		}
		else if(qId1->uriId > qId2->uriId)
		{
			return 1;
		}
	}

	return 0;
}

