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
 * Module: edubtm_Insert.c
 *
 * Description : 
 *  This function edubtm_Insert(...) recursively calls itself until the type
 *  of a root page becomes LEAF.  If the given root page is an internal,
 *  it recursively calls itself using a proper child.  If the result of
 *  the call occur spliting, merging, or redistributing the children, it
 *  may insert, delete, or replace its own internal item, and if the given
 *  root page may be merged, splitted, or redistributed, it affects the
 *  return values.
 *
 * Exports:
 *  Four edubtm_Insert(ObjectID*, PageID*, KeyDesc*, KeyValue*, ObjectID*,
 *                  Boolean*, Boolean*, InternalItem*, Pool*, DeallocListElem*)
 *  Four edubtm_InsertLeaf(ObjectID*, PageID*, BtreeLeaf*, KeyDesc*, KeyValue*,
 *                      ObjectID*, Boolean*, Boolean*, InternalItem*)
 *  Four edubtm_InsertInternal(ObjectID*, BtreeInternal*, InternalItem*,
 *                          Two, Boolean*, InternalItem*)
 */


#include <string.h>
#include "EduBtM_common.h"
#include "BfM.h"
#include "OM_Internal.h"	/* for SlottedPage containing catalog object */
#include "EduBtM_Internal.h"



/*@================================
 * edubtm_Insert()
 *================================*/
/*
 * Function: Four edubtm_Insert(ObjectID*, PageID*, KeyDesc*, KeyValue*,
 *                           ObjectID*, Boolean*, Boolean*, InternalItem*,
 *                           Pool*, DeallocListElem*)
 *
 * Description:
 * (Following description is for original ODYSSEUS/COSMOS BtM.
 *  For ODYSSEUS/EduCOSMOS EduBtM, refer to the EduBtM project manual.)
 *
 *  If the given root is a leaf page, it should get the correct entry in the
 *  leaf. If the entry is already in the leaf, it simply insert it into the
 *  entry and increment the number of ObjectIDs.  If it is not in the leaf it
 *  makes a new entry and insert it into the leaf.
 *  If there is not enough spage in the leaf, the page should be splitted.  The
 *  overflow page may be used or created by this routine. It is created when
 *  the size of the entry is greater than a third of a page.
 * 
 *  'h' is TRUE if the given root page is splitted and the entry item will be
 *  inserted into the parent page.  'f' is TRUE if the given page is not half
 *  full because of creating a new overflow page.
 *
 * Returns:
 *  Error code
 *    eBADBTREEPAGE_BTM
 *    some errors caused by function calls
 */
Four edubtm_Insert(
    ObjectID                    *catObjForFile,         /* IN catalog object of B+-tree file */
    PageID                      *root,                  /* IN the root of a Btree */
    KeyDesc                     *kdesc,                 /* IN Btree key descriptor */
    KeyValue                    *kval,                  /* IN key value */
    ObjectID                    *oid,                   /* IN ObjectID which will be inserted */
    Boolean                     *f,                     /* OUT whether it is merged by creating a new overflow page */
    Boolean                     *h,                     /* OUT whether it is splitted */
    InternalItem                *item,                  /* OUT Internal Item which will be inserted */
                                                        /*     into its parent when 'h' is TRUE */
    Pool                        *dlPool,                /* INOUT pool of dealloc list */
    DeallocListElem             *dlHead)                /* INOUT head of the dealloc list */
{
    Four                        e;                      /* error number */
    Boolean                     lh;                     /* local 'h' */
    Boolean                     lf;                     /* local 'f' */
    Two                         idx;                    /* index for the given key value */
    PageID                      newPid;                 /* a new PageID */
    KeyValue                    tKey;                   /* a temporary key */
    InternalItem                litem;                  /* a local internal item */
    BtreePage                   *apage;                 /* a pointer to the root page */
    btm_InternalEntry           *iEntry;                /* an internal entry */
    Two                         iEntryOffset;           /* starting offset of an internal entry */
    SlottedPage                 *catPage;               /* buffer page containing the catalog object */
    sm_CatOverlayForBtree       *catEntry;              /* pointer to Btree file catalog information */
    PhysicalFileID              pFid;                   /* B+-tree file's FileID */


    /* Error check whether using not supported functionality by EduBtM */
    int i;
    for (i=0; i<kdesc->nparts; i++)
    {
        if (kdesc->kpart[i].type!=SM_INT && kdesc->kpart[i].type!=SM_VARSTRING)
            ERR(eNOTSUPPORTED_EDUBTM);
    }


    /*@ Initially the flags are FALSE */
    *h = *f = FALSE;

	e = BfM_GetTrain(root, &apage, PAGE_BUF);
	if (e < 0) ERR(e);

	/* If root page is internal page */
	if (apage->any.hdr.type & INTERNAL)
	{
		// Decide a child page to find a leaf page to insert into.
		edubtm_BinarySearchInternal(&apage->bi, kdesc, kval, &idx); // return True or False
		
		// Make the child page's PageID and BtreePage.
		if (idx >= 0)
		{
			iEntry = apage->bi.data + apage->bi.slot[-1*idx];
			MAKE_PAGEID(newPid, root->volNo, iEntry->spid);
		}
		else 
		{
			MAKE_PAGEID(newPid, root->volNo, apage->bi.hdr.p0);
		}

		// Insert into the child page (newPid).
		lf = lh = FALSE;
		e = edubtm_Insert(catObjForFile, &newPid, kdesc, kval, oid, &lf, &lh, &litem, dlPool, dlHead);
		if (e < 0) ERR(e);

		// If there is split in child page
		if (lh == TRUE)
		{
			// Decide slotNo in index entry.
			memcpy(&tKey, &litem.klen, sizeof(Two) + sizeof(char) * MAXKEYLEN);
			edubtm_BinarySearchInternal(&apage->bi, kdesc, &tKey, &idx); // parameter?

			// Insert new child page's internal index entry (litem) into root page. 
			e = edubtm_InsertInternal(catObjForFile, apage, &litem, idx, h, item);
			if (e < 0) ERR(e);
		}
	}
	/* If root page is leaf page */
	else if (apage->any.hdr.type & LEAF)
	{
		// Insert into leaf page.
		e = edubtm_InsertLeaf(catObjForFile, root, apage, kdesc, kval, oid, f, h, item);
		if (e < 0) ERR(e);
	}

	e = BfM_SetDirty(root, PAGE_BUF);
	if (e < 0) ERR(e);
	e = BfM_FreeTrain(root, PAGE_BUF);
	if (e < 0) ERR(e);


    return(eNOERROR);
    
}   /* edubtm_Insert() */



/*@================================
 * edubtm_InsertLeaf()
 *================================*/
/*
 * Function: Four edubtm_InsertLeaf(ObjectID*, PageID*, BtreeLeaf*, KeyDesc*,
 *                               KeyValue*, ObjectID*, Boolean*, Boolean*,
 *                               InternalItem*)
 *
 * Description:
 * (Following description is for original ODYSSEUS/COSMOS BtM.
 *  For ODYSSEUS/EduCOSMOS EduBtM, refer to the EduBtM project manual.)
 *
 *  Insert into the given leaf page an ObjectID with the given key.
 *
 * Returns:
 *  Error code
 *    eDUPLICATEDKEY_BTM
 *    eDUPLICATEDOBJECTID_BTM
 *    some errors causd by function calls
 *
 * Side effects:
 *  1) f : TRUE if the leaf page is underflowed by creating an overflow page
 *  2) h : TRUE if the leaf page is splitted by inserting the given ObjectID
 *  3) item : item to be inserted into the parent
 */
Four edubtm_InsertLeaf(
    ObjectID                    *catObjForFile, /* IN catalog object of B+-tree file */
    PageID                      *pid,           /* IN PageID of Leaf Page */
    BtreeLeaf                   *page,          /* INOUT pointer to buffer page of Leaf page */
    KeyDesc                     *kdesc,         /* IN Btree key descriptor */
    KeyValue                    *kval,          /* IN key value */
    ObjectID                    *oid,           /* IN ObjectID which will be inserted */
    Boolean                     *f,             /* OUT whether it is merged by creating */
                                                /*     a new overflow page */
    Boolean                     *h,             /* OUT whether it is splitted */
    InternalItem                *item)          /* OUT Internal Item which will be inserted */
                                                /*     into its parent when 'h' is TRUE */
{
    Four                        e;              /* error number */
    Two                         i;
    Two                         idx;            /* index for the given key value */
    LeafItem                    leaf;           /* a Leaf Item */
    Boolean                     found;          /* search result */
    btm_LeafEntry               *entry;         /* an entry in a leaf page */
    Two                         entryOffset;    /* start position of an entry */
	Two							len;
	Two                         alignedKlen;    /* aligned length of the key length */
    PageID                      ovPid;          /* PageID of an overflow page */
    Two                         entryLen;       /* length of an entry */
	Two							neededSpace;
    ObjectID                    *oidArray;      /* an array of ObjectIDs */
    Two                         oidArrayElemNo; /* an index for the ObjectID array */


    /* Error check whether using not supported functionality by EduBtM */
    for (i=0; i<kdesc->nparts; i++)
    {
        if (kdesc->kpart[i].type != SM_INT && kdesc->kpart[i].type != SM_VARSTRING)
            ERR(eNOTSUPPORTED_EDUBTM);
    }

    
    /*@ Initially the flags are FALSE */
    *h = *f = FALSE;
    
	// Decide index of new index entry.
	if (btm_BinarySearchLeaf(page, kdesc, kval, &idx) == TRUE)
		ERR(eDUPLICATEDKEY_BTM);

	// Calculate needed space size for entry (index entry size + slot size).
	if (kdesc->kpart[0].type == SM_INT)
		len = kdesc->kpart[0].length;
	else
		len = kval->len;
	alignedKlen = ALIGNED_LENGTH(len);
	entryLen = 2*sizeof(Two) + alignedKlen + sizeof(ObjectID);
	neededSpace = entryLen + sizeof(Two);

	// If there is enough free space
	if (BL_FREE(page) >= neededSpace)
	{
		if (BL_CFREE(page) < neededSpace)
			edubtm_CompactLeafPage(page, NIL);

		idx++;
		i = 1;
		entry = page->data + page->hdr.free;
		entry->nObjects = 1;
		entry->klen = kval->len;
		memcpy(entry->kval, kval->val, len);
		memcpy(entry->kval + alignedKlen, oid, sizeof(ObjectID));
		
		for (i=page->hdr.nSlots-1; i>=idx; i--)
			page->slot[-1*(i+1)] = page->slot[-1*i];
		
		page->slot[-1*idx] = page->hdr.free;
		page->hdr.free += entryLen;
		page->hdr.nSlots++;
	}
	// else, split the leaf.
	else
	{
		*h = TRUE;
		leaf.oid = *oid;
		leaf.nObjects = 1;
		memcpy(&leaf.klen, kval, sizeof(KeyValue));		
		edubtm_SplitLeaf(catObjForFile, pid, page, idx, &leaf, item);
	}


    return(eNOERROR);
    
} /* edubtm_InsertLeaf() */



/*@================================
 * edubtm_InsertInternal()
 *================================*/
/*
 * Function: Four edubtm_InsertInternal(ObjectID*, BtreeInternal*, InternalItem*, Two, Boolean*, InternalItem*)
 *
 * Description:
 * (Following description is for original ODYSSEUS/COSMOS BtM.
 *  For ODYSSEUS/EduCOSMOS EduBtM, refer to the EduBtM project manual.)
 *
 *  This routine insert the given internal item into the given page. If there
 *  is not enough space in the page, it should split the page and the new
 *  internal item should be returned for inserting into the parent.
 *
 * Returns:
 *  Error code
 *    some errors caused by function calls
 *
 * Side effects:
 *  h:	TRUE if the page is splitted
 *  ritem: an internal item which will be inserted into parent
 *          if spliting occurs.
 */
Four edubtm_InsertInternal(
    ObjectID            *catObjForFile, /* IN catalog object of B+-tree file */
    BtreeInternal       *page,          /* INOUT Page Pointer */
    InternalItem        *item,          /* IN Iternal item which is inserted */
    Two                 high,           /* IN index in the given page */
    Boolean             *h,             /* OUT whether the given page is splitted */
    InternalItem        *ritem)         /* OUT if the given page is splitted, the internal item may be returned by 'ritem'. */
{
    Four                e;              /* error number */
    Two                 i;              /* index */
    Two                 entryOffset;    /* starting offset of an internal entry */
    Two                 entryLen;       /* length of the new entry */
	Two					alignedKlen;
	btm_InternalEntry   *entry;         /* an internal entry of an internal page */
	Two					neededSpace;


    
    /*@ Initially the flag are FALSE */
    *h = FALSE;
    
    
	// Calculate needed space size for entry (index entry size + slot size).
	alignedKlen = ALIGNED_LENGTH(item->klen);
	entryLen = sizeof(ShortPageID) + sizeof(Two) + alignedKlen;
	neededSpace = entryLen + sizeof(Two);

	// If there is enough free space
	if (BI_FREE(page) >= neededSpace)
	{
		if (BI_CFREE(page) < neededSpace)
			edubtm_CompactInternalPage(page, NIL);
		
		high++;
		entry = page->data + page->hdr.free;
		entry->spid = item->spid;
		entry->klen = item->klen;
		memcpy(&entry->kval, item->kval, item->klen);
		
		for (i = page->hdr.nSlots-1; i >= high; i--)
			page->slot[-1*(i+1)] = page->slot[-1*i];
		
		page->slot[-1*high] = page->hdr.free;
		page->hdr.free += entryLen;
		page->hdr.nSlots++;
	}
	// else, split the internal.
	else
	{
		*h = TRUE;
		e = edubtm_SplitInternal(catObjForFile, page, high, item, ritem);
		if (e < 0) ERR(e);
	}
 

    return(eNOERROR);
    
} /* edubtm_InsertInternal() */

