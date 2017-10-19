/******************************************************************************/
/*                                                                            */
/*    ODYSSEUS/EduCOSMOS Educational-Purpose Object Storage System            */
/*                                                                            */
/*    Developed by Professor Kyu-Young Whang et al.                           */
/*                                                                            */
/*    Database and Multimedia Laboratory                                      */
/*                                                                            */
/*    Computer Science Department and                                         */
/*    Advanced Information Technology Research Center (AITrc)                 */
/*    Korea Advanced Institute of Science and Technology (KAIST)              */
/*                                                                            */
/*    e-mail: kywhang@cs.kaist.ac.kr                                          */
/*    phone: +82-42-350-7722                                                  */
/*    fax: +82-42-350-8380                                                    */
/*                                                                            */
/*    Copyright (c) 1995-2013 by Kyu-Young Whang                              */
/*                                                                            */
/*    All rights reserved. No part of this software may be reproduced,        */
/*    stored in a retrieval system, or transmitted, in any form or by any     */
/*    means, electronic, mechanical, photocopying, recording, or otherwise,   */
/*    without prior written permission of the copyright owner.                */
/*                                                                            */
/******************************************************************************/
/*
 * Module: EduBtM_FetchNext.c
 *
 * Description:
 *  Find the next ObjectID satisfying the given condition. The current ObjectID
 *  is specified by the 'current'.
 *
 * Exports:
 *  Four EduBtM_FetchNext(PageID*, KeyDesc*, KeyValue*, Four, BtreeCursor*, BtreeCursor*)
 */


#include <string.h>
#include "EduBtM_common.h"
#include "BfM.h"
#include "EduBtM_Internal.h"


/*@ Internal Function Prototypes */
Four edubtm_FetchNext(KeyDesc*, KeyValue*, Four, BtreeCursor*, BtreeCursor*);



/*@================================
 * EduBtM_FetchNext()
 *================================*/
/*
 * Function: Four EduBtM_FetchNext(PageID*, KeyDesc*, KeyValue*,
 *                              Four, BtreeCursor*, BtreeCursor*)
 *
 * Description:
 * (Following description is for original ODYSSEUS/COSMOS BtM.
 *  For ODYSSEUS/EduCOSMOS EduBtM, refer to the EduBtM project manual.)
 *
 *  Fetch the next ObjectID satisfying the given condition.
 * By the B+ tree structure modification resulted from the splitting or merging
 * the current cursor may point to the invalid position. So we should adjust
 * the B+ tree cursor before using the cursor.
 *
 * Returns:
 *  error code
 *    eBADPARAMETER_BTM
 *    eBADCURSOR
 *    some errors caused by function calls
 */
Four EduBtM_FetchNext(
    PageID                      *root,          /* IN root page's PageID */
    KeyDesc                     *kdesc,         /* IN key descriptor */
    KeyValue                    *kval,          /* IN key value of stop condition */
    Four                        compOp,         /* IN comparison operator of stop condition */
    BtreeCursor                 *current,       /* IN current B+ tree cursor */
    BtreeCursor                 *next)          /* OUT next B+ tree cursor */
{
    int							i;
    Four                        e;              /* error number */
    Four                        cmp;            /* comparison result */
    Two                         slotNo;         /* slot no. of a leaf page */
    Two                         oidArrayElemNo; /* element no. of the array of ObjectIDs */
    Two                         alignedKlen;    /* aligned length of key length */
    PageID                      overflow;       /* temporary PageID of an overflow page */
    Boolean                     found;          /* search result */
    ObjectID                    *oidArray;      /* array of ObjectIDs */
    BtreeLeaf                   *apage;         /* pointer to a buffer holding a leaf page */
    BtreeOverflow               *opage;         /* pointer to a buffer holding an overflow page */
    btm_LeafEntry               *entry;         /* pointer to a leaf entry */
    BtreeCursor                 tCursor;        /* a temporary Btree cursor */
  
    
    /*@ check parameter */
    if (root == NULL || kdesc == NULL || kval == NULL || current == NULL || next == NULL)
		ERR(eBADPARAMETER_BTM);
    
    /* Is the current cursor valid? */
    if (current->flag != CURSOR_ON && current->flag != CURSOR_EOS)
		ERR(eBADCURSOR);
    
    if (current->flag == CURSOR_EOS) return(eNOERROR);
    
    /* Error check whether using not supported functionality by EduBtM */
    for(i=0; i<kdesc->nparts; i++)
    {
        if(kdesc->kpart[i].type!=SM_INT && kdesc->kpart[i].type!=SM_VARSTRING)
            ERR(eNOTSUPPORTED_EDUBTM);
    }

	e = edubtm_FetchNext(kdesc, kval, compOp, current, next);
	if (e < 0) ERR(e);

    
    return(eNOERROR);
    
} /* EduBtM_FetchNext() */



/*@================================
 * edubtm_FetchNext()
 *================================*/
/*
 * Function: Four edubtm_FetchNext(KeyDesc*, KeyValue*, Four,
 *                              BtreeCursor*, BtreeCursor*)
 *
 * Description:
 * (Following description is for original ODYSSEUS/COSMOS BtM.
 *  For ODYSSEUS/EduCOSMOS EduBtM, refer to the EduBtM project manual.)
 *
 *  Get the next item. We assume that the current cursor is valid; that is.
 *  'current' rightly points to an existing ObjectID.
 *
 * Returns:
 *  Error code
 *    eBADCOMPOP_BTM
 *    some errors caused by function calls
 */
Four edubtm_FetchNext(
    KeyDesc  		*kdesc,		/* IN key descriptor */
    KeyValue 		*kval,		/* IN key value of stop condition */
    Four     		compOp,		/* IN comparison operator of stop condition */
    BtreeCursor 	*current,	/* IN current cursor */
    BtreeCursor 	*next)		/* OUT next cursor */
{
    Four 		e;		/* error number */
    Four 		cmp;		/* comparison result */
    Two 		alignedKlen;	/* aligned length of a key length */
    PageID 		curLeaf;		/* temporary PageID of a leaf page */
	PageID		nextLeaf;
    PageID 		overflow;	/* temporary PageID of an overflow page */
    ObjectID 		*oidArray;	/* array of ObjectIDs */
    BtreeLeaf 		*apage;		/* pointer to a buffer holding a leaf page */
    BtreeOverflow 	*opage;		/* pointer to a buffer holding an overflow page */
    btm_LeafEntry 	*entry;		/* pointer to a leaf entry */    
	Two			idx;

    
    /* Error check whether using not supported functionality by EduBtM */
    int i;
    for (i=0; i<kdesc->nparts; i++)
    {
        if (kdesc->kpart[i].type!=SM_INT && kdesc->kpart[i].type!=SM_VARSTRING)
            ERR(eNOTSUPPORTED_EDUBTM);
    }

	curLeaf = current->leaf;
	nextLeaf = curLeaf;
	next->flag = CURSOR_ON;

	e = BfM_GetTrain(&curLeaf, &apage, PAGE_BUF);	
	if (e < 0) ERR(e);

	// Decide next leaf index.
	idx = current->slotNo;
	if (compOp == SM_EQ) next->flag = CURSOR_EOS;
	else if (compOp == SM_LT || compOp == SM_LE || compOp == SM_EOF) idx += 1;
	else if (compOp == SM_GT || compOp == SM_GE || compOp == SM_BOF) idx -= 1;

	// If the index is not in current leaf, change the leaf and set index.
	if (next->flag == CURSOR_EOS);
	else if (idx < 0)
	{
		if (apage->hdr.prevPage != NIL)
		{
			MAKE_PAGEID(nextLeaf, curLeaf.volNo, apage->hdr.prevPage);
			e = BfM_FreeTrain(&curLeaf, PAGE_BUF);
			if (e < 0) ERR(e);
			e = BfM_GetTrain(&nextLeaf, &apage, PAGE_BUF);
			if (e < 0) ERR(e);
			idx = apage->hdr.nSlots-1;
		}
		else
		{
			next->flag = CURSOR_EOS;
		}
	}
	else if (idx >= apage->hdr.nSlots)
	{
		if (apage->hdr.nextPage != NIL)
		{
			MAKE_PAGEID(nextLeaf, curLeaf.volNo, apage->hdr.nextPage);
			e = BfM_FreeTrain(&curLeaf, PAGE_BUF);
			if (e < 0) ERR(e);
			e = BfM_GetTrain(&nextLeaf, &apage, PAGE_BUF);
			if (e < 0) ERR(e);
			idx = 0;
		}
		else
		{
			next->flag = CURSOR_EOS;
		}
	}
	else
	{
		e = BfM_FreeTrain(&curLeaf, PAGE_BUF);
		if (e < 0) ERR(e);
		e = BfM_GetTrain(&nextLeaf, &apage, PAGE_BUF);
		if (e < 0) ERR(e);
	}

	if (next->flag == CURSOR_EOS)
	{
		e = BfM_FreeTrain(&curLeaf, PAGE_BUF);
		if (e < 0) ERR(e);
	}
	else
	{
		// Make next cursor.
		entry = apage->data + apage->slot[-1*idx];
		alignedKlen = ALIGNED_LENGTH(entry->klen);
		memcpy(&next->oid, &entry->kval + alignedKlen, sizeof(ObjectID));
		memcpy(&next->key, &entry->klen, sizeof(KeyValue));
		next->leaf = nextLeaf;
		next->slotNo = idx;

		cmp = edubtm_KeyCompare(kdesc, &next->key, kval);
		if ((compOp == SM_LT && !(cmp == LESS)) ||
				(compOp == SM_LE && !(cmp == LESS || cmp == EQUAL)) ||
				(compOp == SM_GT && !(cmp == GREATER)) ||
				(compOp == SM_GE && !(cmp == GREATER || cmp == EQUAL)))
			next->flag = CURSOR_EOS;

		e = BfM_FreeTrain(&nextLeaf, PAGE_BUF);
		if (e < 0) ERR(e);
	}
    

    return(eNOERROR);
    
} /* edubtm_FetchNext() */
