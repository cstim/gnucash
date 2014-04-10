/********************************************************************\
 * Account.c -- Account data structure implementation               *
 * Copyright (C) 1997 Robin D. Clark                                *
 * Copyright (C) 1997-2002 Linas Vepstas <linas@linas.org>          *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#include "config.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "AccountP.h"
#include "Backend.h"
#include "BackendP.h"
#include "GNCIdP.h"
#include "Group.h"
#include "GroupP.h"
#include "TransactionP.h"
#include "gnc-date.h"
#include "gnc-engine.h"
#include "gnc-engine-util.h"
#include "gnc-event-p.h"
#include "gnc-lot.h"
#include "gnc-lot-p.h"
#include "gnc-pricedb.h"
#include "kvp_frame.h"
#include "kvp-util-p.h"
#include "messages.h"

#include "qofbook.h"
#include "qofbook-p.h"
#include "qofobject.h"
#include "qofqueryobject.h"

static short module = MOD_ACCOUNT; 


/********************************************************************\
 * Because I can't use C++ for this project, doesn't mean that I    *
 * can't pretend to!  These functions perform actions on the        *
 * account data structure, in order to encapsulate the knowledge    *
 * of the internals of the Account in one file.                     *
\********************************************************************/

static void xaccAccountBringUpToDate (Account *);

/********************************************************************\
\********************************************************************/

G_INLINE_FUNC void account_event (Account *account);
G_INLINE_FUNC void
account_event (Account *account)
{
  gnc_engine_generate_event (&account->guid, GNC_EVENT_MODIFY);
}


G_INLINE_FUNC void mark_account (Account *account);
G_INLINE_FUNC void
mark_account (Account *account)
{
  if (account->parent)
    account->parent->saved = FALSE;

  account_event (account);
}

/********************************************************************\
\********************************************************************/

static void
xaccInitAccount (Account * acc, QofBook *book)
{
  acc->parent   = NULL;
  acc->children = NULL;

  acc->balance = gnc_numeric_zero();
  acc->cleared_balance = gnc_numeric_zero();
  acc->reconciled_balance = gnc_numeric_zero();

  acc->starting_balance = gnc_numeric_zero();
  acc->starting_cleared_balance = gnc_numeric_zero();
  acc->starting_reconciled_balance = gnc_numeric_zero();

  acc->type = NO_TYPE;

  acc->accountName = g_strdup("");
  acc->accountCode = g_strdup("");
  acc->description = g_strdup("");

  acc->kvp_data    = kvp_frame_new();
  acc->idata = 0;

  acc->commodity     = NULL;
  acc->commodity_scu = 0;
  acc->non_standard_scu = FALSE;

  acc->splits = NULL;
  acc->lots = NULL;

  acc->version = 0;
  acc->version_check = 0;
  acc->editlevel = 0;
  acc->balance_dirty = FALSE;
  acc->sort_dirty = FALSE;
  acc->core_dirty = FALSE;
  acc->do_free = FALSE;

  acc->book = book;

  xaccGUIDNew(&acc->guid, book);
  xaccStoreEntity(book->entity_table, acc, &acc->guid, GNC_ID_ACCOUNT);
  LEAVE ("account=%p\n", acc);
}

/********************************************************************\
\********************************************************************/

Account *
xaccMallocAccount (QofBook *book)
{
  Account *acc;

  g_return_val_if_fail (book, NULL);

  acc = g_new (Account, 1);

  xaccInitAccount (acc, book);

  gnc_engine_generate_event (&acc->guid, GNC_EVENT_CREATE);

  return acc;
}

Account *
xaccCloneAccountSimple(const Account *from, QofBook *book)
{
    Account *ret;

    if (!from || !book) return NULL;
    ENTER (" ");

    ret = g_new (Account, 1);
    g_return_val_if_fail (ret, NULL);

    xaccInitAccount (ret, book);

    /* Do not Begin/CommitEdit() here; give the caller 
     * a chance to fix things up, and let them do it.
     * Also let caller issue the generate_event (EVENT_CREATE) */
    ret->type = from->type;

    ret->accountName = g_strdup(from->accountName);
    ret->accountCode = g_strdup(from->accountCode);
    ret->description = g_strdup(from->description);

    ret->kvp_data    = kvp_frame_copy(from->kvp_data);

    ret->commodity    = from->commodity;
    ret->commodity_scu = from->commodity_scu;
    ret->non_standard_scu = from->non_standard_scu;
    ret->core_dirty   = TRUE;

    LEAVE (" ");
    return ret;
}

Account *
xaccCloneAccount (const Account *from, QofBook *book)
{
    time_t now;
    Account *ret;

    if (!from || !book) return NULL;
    ENTER (" ");

    ret = g_new (Account, 1);
    g_return_val_if_fail (ret, NULL);

    now = time(0);
    xaccInitAccount (ret, book);

    /* Do not Begin/CommitEdit() here; give the caller 
     * a chance to fix things up, and let them do it.
     * Also let caller issue the generate_event (EVENT_CREATE) */
    ret->type = from->type;

    ret->accountName = g_strdup(from->accountName);
    ret->accountCode = g_strdup(from->accountCode);
    ret->description = g_strdup(from->description);

    ret->kvp_data    = kvp_frame_copy(from->kvp_data);

    ret->commodity    = from->commodity;
    ret->commodity_scu = from->commodity_scu;
    ret->non_standard_scu = from->non_standard_scu;
    ret->core_dirty   = TRUE;

    /* make a note of where the copy came from */
    gnc_kvp_gemini (ret->kvp_data, &from->guid, &from->book->guid, now);
    gnc_kvp_gemini (from->kvp_data, &ret->guid, &book->guid, now);

    LEAVE (" ");
    return ret;
}

/* ================================================================ */

Account *
xaccAccountLookupTwin (Account *acc,  QofBook *book)
{
   kvp_value *v_ncopies;
   int i, ncopies = 0;

   if (!acc || !book) return NULL;
   ENTER (" ");

   v_ncopies = kvp_frame_get_slot_path (acc->kvp_data, "gemini", "ncopies", NULL);
   if (!v_ncopies) return NULL;
   ncopies = kvp_value_get_gint64 (v_ncopies);
   for (i=0; i<ncopies; i++)
   {
      GUID * book_guid;
      kvp_value *v_book_guid;
      char buff[80];

      sprintf (buff, "%d", i);
      v_book_guid = kvp_frame_get_slot_path (acc->kvp_data, 
             "gemini", buff, "book_guid", NULL);
      if (!v_book_guid) continue;
      book_guid = kvp_value_get_guid (v_book_guid);

      if (guid_equal(book_guid, &book->guid))
      {
         Account *twin;
         GUID * acct_guid;
         kvp_value *v_acct_guid;

         v_acct_guid = kvp_frame_get_slot_path (acc->kvp_data, 
             "gemini", buff, "acct_guid", NULL);
         if (!v_acct_guid) return NULL;
         acct_guid = kvp_value_get_guid (v_acct_guid);

         twin = xaccAccountLookup (acct_guid, book);
         return twin;
      }
   }
   LEAVE (" ");
   return NULL;
}

/********************************************************************\
\********************************************************************/

void
xaccFreeAccount (Account *acc)
{
  Transaction *t;
  GList *lp;

  if (!acc || !acc->book) return;

  gnc_engine_generate_event (&acc->guid, GNC_EVENT_DESTROY);

  xaccRemoveEntity (acc->book->entity_table, &acc->guid);

  if (acc->children) 
  {
    PERR (" instead of calling xaccFreeAccount(), please call \n"
          " xaccAccountBeginEdit(); xaccAccountDestroy(); \n");

    /* First, recursively free children */
    xaccFreeAccountGroup (acc->children);
    acc->children = NULL;
  }

  /* remove lots -- although these should be gone by now. */
  if (acc->lots)
  {
    PERR (" instead of calling xaccFreeAccount(), please call \n"
          " xaccAccountBeginEdit(); xaccAccountDestroy(); \n");
  
    for (lp=acc->lots; lp; lp=lp->next)
    {
      GNCLot *lot = lp->data;
      gnc_lot_destroy (lot);
    }
    g_list_free (acc->lots);
    acc->lots = NULL;
  }

  /* Next, clean up the splits */
  /* NB there shouldn't be any splits by now ... they should 
   * have been all been freed by CommitEdit().  We can remove this
   * check once we know the warning isn't occurring any more. */
  if (acc->splits) 
  {
    PERR (" instead of calling xaccFreeAccount(), please call \n"
          " xaccAccountBeginEdit(); xaccAccountDestroy(); \n");
  
    /* any split pointing at this account needs to be unmarked */
    for(lp = acc->splits; lp; lp = lp->next) 
    {
      Split *s = lp->data;
      s->acc = NULL;
    }
  
    acc->editlevel = 0;
  
    for(lp = acc->splits; lp; lp = lp->next) {
      Split *s = (Split *) lp->data;
      t = s->parent;
      xaccTransBeginEdit (t);
      xaccSplitDestroy (s);
      xaccTransCommitEdit (t);
    }
  
    /* free up array of split pointers */
    g_list_free(acc->splits);
    acc->splits = NULL;
  }

  if (acc->accountName) g_free (acc->accountName);
  acc->accountName = NULL;
  if (acc->accountCode) g_free (acc->accountCode);
  acc->accountCode = NULL;
  if (acc->description) g_free (acc->description);
  acc->description = NULL;

  kvp_frame_delete (acc->kvp_data);
  acc->kvp_data = NULL;

  /* zero out values, just in case stray 
   * pointers are pointing here. */

  acc->commodity = NULL;

  acc->parent   = NULL;
  acc->children = NULL;

  acc->balance  = gnc_numeric_zero();
  acc->cleared_balance = gnc_numeric_zero();
  acc->reconciled_balance = gnc_numeric_zero();

  acc->type = NO_TYPE;

  acc->accountName = NULL;
  acc->description = NULL;
  acc->commodity   = NULL;

  acc->version = 0;
  acc->editlevel = 0;
  acc->balance_dirty = FALSE;
  acc->sort_dirty = FALSE;
  acc->core_dirty = FALSE;

  g_free(acc);
}

/********************************************************************\
 * transactional routines
\********************************************************************/

void 
xaccAccountBeginEdit (Account *acc) 
{
  Backend * be;
  if (!acc) return;

  acc->editlevel++;
  if (1 < acc->editlevel) return;

  if (0 >= acc->editlevel) 
  {
    PERR ("unbalanced call - resetting (was %d)", acc->editlevel);
    acc->editlevel = 1;
  }

  acc->core_dirty = FALSE;

  /* See if there's a backend.  If there is, invoke it. */
  be = xaccAccountGetBackend (acc);
  if (be && be->begin) {
     (be->begin) (be, GNC_ID_ACCOUNT, acc);
  }
}

void 
xaccAccountCommitEdit (Account *acc) 
{
  Backend * be;

  if (!acc) return;

  acc->editlevel--;
  if (0 < acc->editlevel) return;

  if (0 > acc->editlevel) 
  {
    PERR ("unbalanced call - resetting (was %d)", acc->editlevel);
    acc->editlevel = 0;
  }

  /* If marked for deletion, get rid of subaccounts first,
   * and then the splits ... */
  if (acc->do_free)
  {
    GList *lp;
 
    acc->editlevel++;

    /* First, recursively free children */
    xaccFreeAccountGroup (acc->children);
    acc->children = NULL;

    PINFO ("freeing splits for account %p (%s)\n",
           acc, acc->accountName ? acc->accountName : "(null)");

    while (acc->splits)
    {
      Split *s = acc->splits->data;
      Transaction *t = s->parent;

      xaccTransBeginEdit (t);
      xaccSplitDestroy (s);
      xaccTransCommitEdit (t);
    }

    /* the lots should be empty by now */
    for (lp=acc->lots; lp; lp=lp->next)
    {
      GNCLot *lot = lp->data;
      gnc_lot_destroy (lot);
    }
    g_list_free (acc->lots);
    acc->lots = NULL;

    acc->core_dirty = TRUE;
    acc->editlevel--;
  }
  else 
  {
    xaccAccountBringUpToDate(acc);

    /* force re-sort of parent group */
    xaccGroupInsertAccount(acc->parent, acc); 
  }

  /* See if there's a backend.  If there is, invoke it. */
  be = xaccAccountGetBackend (acc);
  if (be && be->commit) 
  {
    GNCBackendError errcode;

    /* clear errors */
    do {
      errcode = xaccBackendGetError (be);
    } while (ERR_BACKEND_NO_ERR != errcode);

    (be->commit) (be, GNC_ID_ACCOUNT, acc);
    errcode = xaccBackendGetError (be);

    if (ERR_BACKEND_NO_ERR != errcode)
    {
      char * err;
      /* destroys must be rolled back as well ... ??? */
      acc->do_free = FALSE;
      /* XXX hack alert FIXME implement account rollback */
      PERR (" backend asked engine to rollback, but this isn't"
            " handled yet. Return code=%d", errcode);
      err = xaccBackendGetMessage(be);
      /* g_strdup here, because err needs to be g_freed if from Backend */
      err = err ? err : g_strdup(_("Error message not available"));
      /* Translators: %d is the (internal) error number. %s is the
       * human-readable error description. */
      PWARN_GUI(_("Error occurred while saving Account:\n%d: %s"),
		      xaccBackendGetError(be), err);
    
      /* push error back onto the stack */
      xaccBackendSetError (be, errcode);
      xaccBackendSetMessage (be, err);
      g_free(err);
    }
  }
  acc->core_dirty = FALSE;

  /* final stages of freeing the account */
  if (acc->do_free)
  {
    xaccGroupRemoveAccount(acc->parent, acc);
    xaccFreeAccount(acc);
  }
}

void 
xaccAccountDestroy (Account *acc) 
{
  if (!acc) return;
  acc->do_free = TRUE;

  xaccAccountCommitEdit (acc);
}


void 
xaccAccountSetVersion (Account *acc, gint32 vers)
{
  if (!acc) return;
  acc->version = vers;
}

gint32 
xaccAccountGetVersion (Account *acc)
{
  if (!acc) return 0;
  return (acc->version);
}

QofBook *
xaccAccountGetBook (Account *account)
{
  if (!account) return NULL;
  return account->book;
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountEqual(Account *aa, Account *ab, gboolean check_guids)
{
  if(!aa && !ab) return TRUE;

  if(!aa || !ab)
  {
    PWARN ("one is NULL");
    return FALSE;
  }

  if (aa->type != ab->type)
  {
    PWARN ("types differ: %d vs %d", aa->type, ab->type);
    return FALSE;
  }

  if (safe_strcmp(aa->accountName, ab->accountName) != 0)
  {
    PWARN ("names differ: %s vs %s", aa->accountName, ab->accountName);
    return FALSE;
  }

  if (safe_strcmp(aa->accountCode, ab->accountCode) != 0)
  {
    PWARN ("codes differ: %s vs %s", aa->accountCode, ab->accountCode);
    return FALSE;
  }

  if (safe_strcmp(aa->description, ab->description) != 0)
  {
    PWARN ("descriptions differ: %s vs %s", aa->description, ab->description);
    return FALSE;
  }

  if (!gnc_commodity_equal(aa->commodity, ab->commodity))
  {
    PWARN ("commodities differ");
    return FALSE;
  }

  if(check_guids) {
    if(!guid_equal(&aa->guid, &ab->guid))
    {
      PWARN ("GUIDs differ");
      return FALSE;
    }
  }

  if (kvp_frame_compare(aa->kvp_data, ab->kvp_data) != 0)
  {
    char *frame_a;
    char *frame_b;

    frame_a = kvp_frame_to_string (aa->kvp_data);
    frame_b = kvp_frame_to_string (ab->kvp_data);

    PWARN ("kvp frames differ:\n%s\n\nvs\n\n%s", frame_a, frame_b);

    g_free (frame_a);
    g_free (frame_b);

    return FALSE;
  }

  if (!gnc_numeric_equal (aa->starting_balance, ab->starting_balance))
  {
    char *str_a;
    char *str_b;

    str_a = gnc_numeric_to_string (aa->starting_balance);
    str_b = gnc_numeric_to_string (ab->starting_balance);

    PWARN ("starting balances differ: %s vs %s", str_a, str_b);

    g_free (str_a);
    g_free (str_b);

    return FALSE;
  }

  if (!gnc_numeric_equal (aa->starting_cleared_balance,
                          ab->starting_cleared_balance))
  {
    char *str_a;
    char *str_b;

    str_a = gnc_numeric_to_string (aa->starting_cleared_balance);
    str_b = gnc_numeric_to_string (ab->starting_cleared_balance);

    PWARN ("starting cleared balances differ: %s vs %s", str_a, str_b);

    g_free (str_a);
    g_free (str_b);

    return FALSE;
  }

  if (!gnc_numeric_equal (aa->starting_reconciled_balance,
                          ab->starting_reconciled_balance))
  {
    char *str_a;
    char *str_b;

    str_a = gnc_numeric_to_string (aa->starting_reconciled_balance);
    str_b = gnc_numeric_to_string (ab->starting_reconciled_balance);

    PWARN ("starting reconciled balances differ: %s vs %s", str_a, str_b);

    g_free (str_a);
    g_free (str_b);

    return FALSE;
  }

  if (!gnc_numeric_equal (aa->balance, ab->balance))
  {
    char *str_a;
    char *str_b;

    str_a = gnc_numeric_to_string (aa->balance);
    str_b = gnc_numeric_to_string (ab->balance);

    PWARN ("balances differ: %s vs %s", str_a, str_b);

    g_free (str_a);
    g_free (str_b);

    return FALSE;
  }

  if (!gnc_numeric_equal (aa->cleared_balance, ab->cleared_balance))
  {
    char *str_a;
    char *str_b;

    str_a = gnc_numeric_to_string (aa->cleared_balance);
    str_b = gnc_numeric_to_string (ab->cleared_balance);

    PWARN ("cleared balances differ: %s vs %s", str_a, str_b);

    g_free (str_a);
    g_free (str_b);

    return FALSE;
  }

  if (!gnc_numeric_equal (aa->reconciled_balance, ab->reconciled_balance))
  {
    char *str_a;
    char *str_b;

    str_a = gnc_numeric_to_string (aa->reconciled_balance);
    str_b = gnc_numeric_to_string (ab->reconciled_balance);

    PWARN ("reconciled balances differ: %s vs %s", str_a, str_b);

    g_free (str_a);
    g_free (str_b);

    return FALSE;
  }

  /* no parent; always compare downwards. */

  {
    GList *la = aa->splits;
    GList *lb = ab->splits;

    if ((la && !lb) || (!la && lb))
    {
      PWARN ("only one has splits");
      return FALSE;
    }

    if(la && lb)
    {
      /* presume that the splits are in the same order */
      while (la && lb)
      {
        Split *sa = (Split *) la->data;
        Split *sb = (Split *) lb->data;

        if (!xaccSplitEqual(sa, sb, check_guids, TRUE, FALSE))
        {
          PWARN ("splits differ");
          return(FALSE);
        }

        la = la->next;
        lb = lb->next;
      }

      if ((la != NULL) || (lb != NULL))
      {
        PWARN ("number of splits differs");
        return(FALSE);
      }
    }
  }

  if (!xaccGroupEqual(aa->children, ab->children, check_guids))
  {
    PWARN ("children differ");
    return FALSE;
  }

  return(TRUE);
}

/********************************************************************\
\********************************************************************/

static gint
split_sort_func(gconstpointer a, gconstpointer b) {
  /* don't coerce xaccSplitDateOrder so we'll catch changes */
  Split *sa = (Split *) a;
  Split *sb = (Split *) b;
  return(xaccSplitDateOrder(sa, sb));
}

void
xaccAccountSortSplits (Account *acc, gboolean force)
{
  if(!acc) return;
  if(!acc->sort_dirty) return;
  if(!force && acc->editlevel > 0) return;

  acc->splits = g_list_sort(acc->splits, split_sort_func);

  acc->sort_dirty = FALSE;
  acc->balance_dirty = TRUE;
}

static void
xaccAccountBringUpToDate(Account *acc) 
{
  if(!acc) return;

  /* if a re-sort happens here, then everything will update, so the
     cost basis and balance calls are no-ops */
  xaccAccountSortSplits(acc, FALSE);
  xaccAccountRecomputeBalance(acc);
}


/********************************************************************
 * xaccAccountGetSlots
 ********************************************************************/

kvp_frame * 
xaccAccountGetSlots(Account * account) 
{
  if (!account) return NULL;
  return(account->kvp_data);
}

void
xaccAccountSetSlots_nc(Account *account, kvp_frame *frame)
{
  if (!account) return;

  xaccAccountBeginEdit (account);
  if (account->kvp_data && frame != account->kvp_data)
  {
      kvp_frame_delete (account->kvp_data);
  }
  account->kvp_data = frame;
  account->core_dirty = TRUE;
  mark_account (account);
  xaccAccountCommitEdit (account);
}


/********************************************************************\
\********************************************************************/

const GUID *
xaccAccountGetGUID (Account *account)
{
  if (!account)
    return xaccGUIDNULL();

  return &account->guid;
}

GUID
xaccAccountReturnGUID (Account *account)
{
  if (!account)
    return *xaccGUIDNULL();

  return account->guid;
}

/********************************************************************\
\********************************************************************/

void 
xaccAccountSetGUID (Account *account, const GUID *guid)
{
  if (!account || !guid) return;

  PINFO("acct=%p", account);
  xaccAccountBeginEdit (account);
  xaccRemoveEntity (account->book->entity_table, &account->guid);

  account->guid = *guid;

  xaccStoreEntity (account->book->entity_table, account,
                   &account->guid, GNC_ID_ACCOUNT);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

Account *
xaccAccountLookup (const GUID *guid, QofBook *book)
{
  if (!guid || !book) return NULL;
  return xaccLookupEntity (qof_book_get_entity_table (book),
                           guid, GNC_ID_ACCOUNT);
}

Account *
xaccAccountLookupDirect (GUID guid, QofBook *book)
{
  if (!book) return NULL;
  return xaccLookupEntity (qof_book_get_entity_table (book),
                           &guid, GNC_ID_ACCOUNT);
}

/********************************************************************\
\********************************************************************/

short
xaccAccountGetMark (Account *acc)
{
  if (!acc) return 0;
  return acc->mark;
}

void
xaccAccountSetMark (Account *acc, short m)
{
  if (!acc) return;
  acc->mark = m;
}

void
xaccClearMark (Account *acc, short val)
{
  AccountGroup *topgrp;

  if (!acc) return;
  topgrp = xaccAccountGetRoot (acc);

  if (topgrp)
  {
    GList *list;
    GList *node;

    list = xaccGroupGetAccountList (topgrp);

    for (node = list; node; node = node->next)
    {
      Account *account = node->data;

      xaccClearMarkDown (account, val);
    }
  }
  else
    xaccClearMarkDown (acc, val);
}

void
xaccClearMarkDown (Account *acc, short val)
{
  AccountGroup *children;

  if (!acc) return;
  acc->mark = val;

  children = acc->children;
  if (children)
  {
    GList *list;
    GList *node;

    list = xaccGroupGetAccountList (children);

    for (node = list; node; node = node->next)
    {
      Account *account = node->data;

      xaccClearMarkDown (account, val);
    }
  }
}

void
xaccClearMarkDownGr (AccountGroup *grp, short val)
{
  GList *list;
  GList *node;

  if (!grp) return;

  list = xaccGroupGetAccountList (grp);

  for (node = list; node; node = node->next)
  {
    Account *account = node->data;

    xaccClearMarkDown (account, val);
  }
}

/********************************************************************\
\********************************************************************/

void
xaccAccountRemoveLot (Account *acc, GNCLot *lot)
{
  if (!acc || !lot) return;
   ENTER ("(acc=%p, lot=%p)", acc, lot);

   xaccAccountBeginEdit (acc);
   acc->lots = g_list_remove (acc->lots, lot);
   xaccAccountCommitEdit (acc);
   LEAVE ("(acc=%p, lot=%p)", acc, lot);
}

void
xaccAccountInsertLot (Account *acc, GNCLot *lot)
{
   GList *sl;
   Account * old_acc = NULL;

   if (!acc || !lot) return;
   ENTER ("(acc=%p, lot=%p)", acc, lot);

   /* pull it out of the old account */
   if (lot->account && lot->account != acc)
   {
      old_acc = lot->account;
      xaccAccountBeginEdit (old_acc);
      old_acc->lots = g_list_remove (old_acc->lots, lot);
      
   }
   
   xaccAccountBeginEdit(acc);

   /* Avoid inserting into list more than once */
   if (lot->account != acc)
   {
      acc->lots = g_list_prepend (acc->lots, lot);
      lot->account = acc;
   }

   /* Move all splits over to the new account.  At worst,
    * this is a no-op. */
   if (lot->splits)
   {
      for (sl = lot->splits; sl; sl=sl->next)
      {
         Split *s = sl->data;
         if (s->acc != acc)
         {
            xaccAccountInsertSplit (acc, s);
         }
      }
   }
   xaccAccountCommitEdit(acc);
   xaccAccountCommitEdit(old_acc);
   LEAVE ("(acc=%p, lot=%p)", acc, lot);
}

/********************************************************************\
\********************************************************************/

void
xaccAccountInsertSplit (Account *acc, Split *split)
{
  Transaction *trans;

  if (!acc) return;
  if (!split) return;
  ENTER ("(acc=%p, split=%p)", acc, split);

  /* check for book mix-up */
  g_return_if_fail (acc->book == split->book);

  trans = xaccSplitGetParent (split);

  xaccAccountBeginEdit(acc);
  xaccTransBeginEdit(trans);

  acc->balance_dirty = TRUE;
  acc->sort_dirty = TRUE;

  /* convert the split to the new account's denominator */
  /* if the denominator can't be exactly converted, it's an error */
  /* FIXME : need to enforce ordering of insertion/value */
  split->amount = gnc_numeric_convert(split->amount, 
                                      xaccAccountGetCommoditySCU(acc),
                                      GNC_RND_ROUND);

  /* if this split belongs to another account, remove it from there
     * first.  We don't want to ever leave the system in an inconsistent
     * state.  Note that it might belong to the current account if we're
     * just using this call to re-order.  */
  if (xaccSplitGetAccount(split) &&
      xaccSplitGetAccount(split) != acc)
    xaccAccountRemoveSplit (xaccSplitGetAccount(split), split);

  split->acc = acc;
  if (split->lot && (NULL == split->lot->account))
  {
      xaccAccountInsertLot (acc, split->lot);
  }

  if (g_list_index(acc->splits, split) == -1)
  {
      if (acc->editlevel == 1)
      {
          acc->splits = g_list_insert_sorted(acc->splits, split,
                                             split_sort_func);
          acc->sort_dirty = FALSE;
      }
      else
          acc->splits = g_list_prepend(acc->splits, split);

      mark_account (acc);
      if (split->parent)
          gnc_engine_generate_event (&split->parent->guid, GNC_EVENT_MODIFY);
  }

  xaccTransCommitEdit(trans);
  xaccAccountCommitEdit(acc);
  LEAVE ("(acc=%p, split=%p)", acc, split);
}

/********************************************************************\
\********************************************************************/

void
xaccAccountRemoveSplit (Account *acc, Split *split)
{
  if (!acc) return;
  if (!split) return;
  ENTER ("(acc=%p, split=%p)", acc, split);

  xaccAccountBeginEdit(acc);
  {
    GList *node;

    node = g_list_find (acc->splits, split);
    if (!node)
    {
      PERR ("split not in account");
    }
    else
    {
      Transaction *trans = xaccSplitGetParent (split);

      acc->splits = g_list_remove_link (acc->splits, node);
      g_list_free_1 (node);

      acc->balance_dirty = TRUE;

      xaccTransBeginEdit (trans);
      split->acc = NULL;
      if (split->lot)
      {
        gnc_lot_remove_split (split->lot, split);
      }
      xaccTransCommitEdit (trans);

      mark_account (acc);
      if (split->parent)
        gnc_engine_generate_event (&split->parent->guid, GNC_EVENT_MODIFY);
    }
  }
  xaccAccountCommitEdit(acc);
  LEAVE ("(acc=%p, split=%p)", acc, split);
}


/********************************************************************\
 * xaccAccountRecomputeBalance                                      *
 *   recomputes the partial balances and the current balance for    *
 *   this account.                                                  *
 *                                                                  *
 * The way the computation is done depends on whether the partial   *
 * balances are for a monetary account (bank, cash, etc.) or a      *
 * certificate account (stock portfolio, mutual fund).  For bank    *
 * accounts, the invariant amount is the dollar amount. For share   *
 * accounts, the invariant amount is the number of shares. For      *
 * share accounts, the share price fluctuates, and the current      *
 * value of such an account is the number of shares times the       *
 * current share price.                                             *
 *                                                                  *
 * Part of the complexity of this computation stems from the fact   *
 * xacc uses a double-entry system, meaning that one transaction    *
 * appears in two accounts: one account is debited, and the other   *
 * is credited.  When the transaction represents a sale of shares,  *
 * or a purchase of shares, some care must be taken to compute      *
 * balances correctly.  For a sale of shares, the stock account must*
 * be debited in shares, but the bank account must be credited      *
 * in dollars.  Thus, two different mechanisms must be used to      *
 * compute balances, depending on account type.                     *
 *                                                                  *
 * Args:   account -- the account for which to recompute balances   *
 * Return: void                                                     *
\********************************************************************/

void
xaccAccountRecomputeBalance (Account * acc)
{
  gnc_numeric  balance;
  gnc_numeric  cleared_balance; 
  gnc_numeric  reconciled_balance;
  Split *last_split = NULL;
  GList *lp;

  if (NULL == acc) return;
  if (acc->editlevel > 0) return;
  if (!acc->balance_dirty) return;
  if (acc->do_free) return;

  balance            = acc->starting_balance;
  cleared_balance    = acc->starting_cleared_balance;
  reconciled_balance = acc->starting_reconciled_balance;

  for(lp = acc->splits; lp; lp = lp->next) {
    Split *split = (Split *) lp->data;

    balance = gnc_numeric_add_fixed(balance, split->amount);

    if (CREC == split->reconciled)
      cleared_balance = gnc_numeric_add_fixed(cleared_balance, split->amount);

    if (YREC == split->reconciled ||
        FREC == split->reconciled) {
      reconciled_balance =
        gnc_numeric_add_fixed(reconciled_balance, split->amount);
    }

    split->balance = balance;
    split->cleared_balance = cleared_balance;
    split->reconciled_balance = reconciled_balance;

    last_split = split;
  }

  acc->balance = balance;
  acc->cleared_balance = cleared_balance;
  acc->reconciled_balance = reconciled_balance;

  acc->balance_dirty = FALSE;
  account_event (acc);
}

/********************************************************************\
\********************************************************************/

void 
xaccAccountSetStartingBalance(Account *acc,
                              const gnc_numeric start_baln,
                              const gnc_numeric start_cleared_baln,
                              const gnc_numeric start_reconciled_baln)  
{
  if (!acc) return;

  acc->starting_balance = start_baln;
  acc->starting_cleared_balance = start_cleared_baln;
  acc->starting_reconciled_balance = start_reconciled_baln;

  acc->balance_dirty = TRUE;
}

/********************************************************************\
 * xaccAccountFixSplitDateOrder                                     *
 *   check this split to see if the date is in correct order        *
 *   If it is not, reorder the transactions ...                     *
 *                                                                  *
 * Args:   acc   -- the account to check                            *
 *         split -- the split to check                              *
 *                                                                  *
 * Return: int -- non-zero if out of order                          *
\********************************************************************/

void
xaccAccountFixSplitDateOrder (Account * acc, Split *split) 
{
  if (NULL == acc) return;
  if (NULL == split) return;

  if (acc->do_free) return;

  acc->sort_dirty = TRUE;
  acc->balance_dirty = TRUE;

  if (acc->editlevel > 0) return;

  xaccAccountBringUpToDate (acc);
}

/********************************************************************\
 * xaccCheckTransDateOrder                                          *
 *   check this transaction to see if the date is in correct order  *
 *   If it is not, reorder the transactions.                        *
 *   This routine perfroms the check for both of the double-entry   *
 *   transaction entries.                                           *
 *                                                                  *
 * Args:   trans -- the transaction to check                        *
 * Return: int -- non-zero if out of order                          *
\********************************************************************/

void
xaccTransFixSplitDateOrder (Transaction *trans)
{
  GList *node;

  if (trans == NULL) return;

  gnc_engine_suspend_events();
  for (node = trans->splits; node; node = node->next)
  {
    Split *s = node->data;
    xaccAccountFixSplitDateOrder (xaccSplitGetAccount(s), s);
  }
  gnc_engine_resume_events();
}

/********************************************************************\
\********************************************************************/

/* The sort order is used to implicitly define an 
 * order for report generation */

static int typeorder[NUM_ACCOUNT_TYPES] = {
     BANK, STOCK, MUTUAL, CURRENCY, CASH, ASSET, RECEIVABLE,
     CREDIT, LIABILITY, PAYABLE, INCOME, EXPENSE, EQUITY };

static int revorder[NUM_ACCOUNT_TYPES] = {
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };


int
xaccAccountOrder (Account **aa, Account **ab) 
{
  char *da, *db;
  char *endptr = NULL;
  int ta, tb;
  long la, lb;

  if ( (*aa) && !(*ab) ) return -1;
  if ( !(*aa) && (*ab) ) return +1;
  if ( !(*aa) && !(*ab) ) return 0;

  /* sort on accountCode strings */
  da = (*aa)->accountCode;
  db = (*ab)->accountCode;

  /* If accountCodes are both base 36 integers do an integer sort */
  la = strtoul (da, &endptr, 36);
  if((*da != '\0') && (*endptr == '\0')) {
    lb = strtoul (db, &endptr, 36);
    if((*db != '\0') && (*endptr == '\0')) {
      if (la < lb) return -1;
      if (la > lb) return +1;
    }
  }

  /* Otherwise do a string sort */
  SAFE_STRCMP (da, db);

  /* if acccount-type-order array not initialized, initialize it */
  /* this will happen at most once during program invocation */
  if (-1 == revorder[0]) {
    int i;
    for (i=0; i<NUM_ACCOUNT_TYPES; i++) {
      revorder [typeorder[i]] = i;
    }
  }

  /* otherwise, sort on account type */
  ta = (*aa)->type;
  tb = (*ab)->type;
  ta = revorder[ta];
  tb = revorder[tb];
  if (ta < tb) return -1;
  if (ta > tb) return +1;

  /* otherwise, sort on accountName strings */
  da = (*aa)->accountName;
  db = (*ab)->accountName;
  SAFE_STRCMP (da, db);

  /* guarantee a stable sort */
  return guid_compare (&((*aa)->guid), &((*ab)->guid));
}

/********************************************************************\
\********************************************************************/

void 
xaccAccountSetType (Account *acc, GNCAccountType tip) 
{

  if (!acc) return;

  xaccAccountBeginEdit(acc);
  {
    /* refuse invalid account types, and don't bother if not new type. */
    if((NUM_ACCOUNT_TYPES > tip) && (acc->type != tip)) {
      acc->type = tip;
      acc->balance_dirty = TRUE; /* new type may affect balance computation */
    }

    mark_account (acc);
  }
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

void 
xaccAccountSetName (Account *acc, const char *str) 
{
   char * tmp;

   if ((!acc) || (!str)) return;

   xaccAccountBeginEdit(acc);
   {
     /* make strdup before freeing (just in case str==accountName !!) */
     tmp = g_strdup (str);
     g_free (acc->accountName);
     acc->accountName = tmp;

     mark_account (acc);
   }
   acc->core_dirty = TRUE;
   xaccAccountCommitEdit(acc);
}

void 
xaccAccountSetCode (Account *acc, const char *str) 
{
   char * tmp;
   if ((!acc) || (!str)) return;

   xaccAccountBeginEdit(acc);
   {
     /* make strdup before freeing */
     tmp = g_strdup (str);
     g_free (acc->accountCode);
     acc->accountCode = tmp;

     mark_account (acc);
   }
   acc->core_dirty = TRUE;
   xaccAccountCommitEdit(acc);
}

void
xaccAccountSetDescription (Account *acc, const char *str) 
{
   char * tmp;
   if ((!acc) || (!str)) return;

   xaccAccountBeginEdit(acc);
   {
     /* make strdup before freeing (just in case str==description !!) */
     tmp = g_strdup (str);
     g_free (acc->description);
     acc->description = tmp;

     mark_account (acc);
   }
   acc->core_dirty = TRUE;
   xaccAccountCommitEdit(acc);
}

void
xaccAccountSetNotes (Account *acc, const char *str) 
{
  if ((!acc) || (!str)) return;

  xaccAccountBeginEdit(acc);
  kvp_frame_set_slot_nc(acc->kvp_data, "notes", 
                        kvp_value_new_string(str));
  mark_account (acc);
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

/* FIXME : is this the right way to do this? */
static void
update_split_commodity(Account * acc) 
{
  GList *lp;

  if(!acc) return;

  xaccAccountBeginEdit(acc);

  /* iterate over splits */
  for (lp = acc->splits; lp; lp = lp->next)
  {
    Split *s = (Split *) lp->data;
    Transaction *trans = xaccSplitGetParent (s);

    xaccTransBeginEdit (trans);
    s->amount = gnc_numeric_convert(s->amount,
				    xaccAccountGetCommoditySCU(acc),
                                    GNC_RND_ROUND);
    xaccTransCommitEdit (trans);
  }

  xaccAccountCommitEdit(acc);
}

/********************************************************************\
\********************************************************************/

void 
xaccAccountSetCommodity (Account * acc, gnc_commodity * com) 
{
  if ((!acc) || (!com)) return;

  xaccAccountBeginEdit(acc);
  {
    acc->commodity    = com;
    acc->commodity_scu = gnc_commodity_get_fraction(com);
    update_split_commodity(acc);

    acc->sort_dirty = TRUE;
    acc->balance_dirty = TRUE;

    mark_account (acc);
  }
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
  if (gnc_commodity_is_iso(com)) {
    /* compatability hack - Gnucash 1.8 gets currency quotes when a
       non-default currency is assigned to an account.  */
    gnc_commodity_set_quote_flag(com, TRUE);
  }
}

void
xaccAccountSetCommoditySCU (Account *acc, int scu)
{
  if (!acc) return;

  xaccAccountBeginEdit(acc);
  {
    acc->commodity_scu = scu;
    mark_account (acc);
  }
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

/*
 * Set the account scu and then check to see if it is the same as the
 * commodity scu.  This function is called when parsing the data file
 * and is designed to catch cases where the two were accidentally set
 * to mismatched values in the past.
 */
void
xaccAccountSetCommoditySCUandFlag (Account *acc, int scu)
{
  if (!acc) return;

  xaccAccountBeginEdit(acc);
  {
    acc->commodity_scu = scu;
    if (scu != gnc_commodity_get_fraction(acc->commodity))
      acc->non_standard_scu = TRUE;
    mark_account (acc);
  }
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

int
xaccAccountGetCommoditySCUi (Account * acc) 
{
  if (!acc) return 0;

  return acc->commodity_scu;
}

int
xaccAccountGetCommoditySCU (Account * acc) 
{
  if (!acc) return 0;

  if (acc->non_standard_scu || !acc->commodity)
    return acc->commodity_scu;
  return gnc_commodity_get_fraction(acc->commodity);
}

void
xaccAccountSetNonStdSCU (Account *acc, gboolean flag)
{
  if (!acc) return;

  xaccAccountBeginEdit(acc);
  {
    acc->non_standard_scu = flag;
    mark_account (acc);
  }
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

gboolean
xaccAccountGetNonStdSCU (Account * acc) 
{
  if (!acc) return 0;

  return acc->non_standard_scu;
}

/********************************************************************\
\********************************************************************/
/* below follow the old, deprecated currency/security routines. */

void 
DxaccAccountSetCurrency (Account * acc, gnc_commodity * currency)
{
  const char *string;
  gnc_commodity *commodity;

  if ((!acc) || (!currency)) return;

  xaccAccountBeginEdit(acc);
  string = gnc_commodity_get_unique_name (currency);
  kvp_frame_set_slot_nc(acc->kvp_data, "old-currency",
                        kvp_value_new_string(string));
  mark_account (acc);
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);

  commodity = DxaccAccountGetCurrency (acc);
  if (!commodity)
  {
    gnc_commodity_table_insert (gnc_commodity_table_get_table (acc->book), currency);
  }
}

void 
DxaccAccountSetSecurity (Account *acc, gnc_commodity * security)
{
  const char *string;
  gnc_commodity *commodity;

  if ((!acc) || (!security)) return;

  xaccAccountBeginEdit(acc);
  string = gnc_commodity_get_unique_name (security);
  kvp_frame_set_slot_nc(acc->kvp_data, "old-security",
                        kvp_value_new_string(string));
  mark_account (acc);
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);

  commodity = DxaccAccountGetSecurity (acc);
  if (!commodity)
  {
    gnc_commodity_table_insert (gnc_commodity_table_get_table (acc->book), security);
  }
}

void 
DxaccAccountSetCurrencySCU (Account * acc, int scu) 
{
  if (!acc) return;

  xaccAccountBeginEdit(acc);
  kvp_frame_set_slot_nc(acc->kvp_data, "old-currency-scu",
                        kvp_value_new_gint64(scu));
  mark_account (acc);
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

int
DxaccAccountGetCurrencySCU (Account * acc) 
{
  kvp_value *v;

  if (!acc) return 0;

  v = kvp_frame_get_slot(acc->kvp_data, "old-currency-scu");
  if (v) return kvp_value_get_gint64 (v);

  return 0;
}

/********************************************************************\
\********************************************************************/

void
xaccAccountDeleteOldData (Account *account)
{
  if (!account) return;

  kvp_frame_set_slot_nc (account->kvp_data, "old-currency", NULL);
  kvp_frame_set_slot_nc (account->kvp_data, "old-security", NULL);
  kvp_frame_set_slot_nc (account->kvp_data, "old-currency-scu", NULL);
  kvp_frame_set_slot_nc (account->kvp_data, "old-security-scu", NULL);
}

/********************************************************************\
\********************************************************************/

AccountGroup *
xaccAccountGetChildren (Account *acc)
{
   if (!acc) return NULL;
   return (acc->children);
}

AccountGroup *
xaccAccountGetParent (Account *acc)
{
   if (!acc) return NULL;
   return (acc->parent);
}

Account *
xaccAccountGetParentAccount (Account * acc)
{
  if (!acc) return NULL;
  return xaccGroupGetParentAccount(acc->parent);
}

GList *
xaccAccountGetDescendants (Account *acc)
{
   GList *accounts;

   if (!acc) return NULL;
   accounts = xaccGroupGetSubAccounts(acc->children);
   return (accounts);
}

GNCAccountType
xaccAccountGetType (Account *acc)
{
   if (!acc) return NO_TYPE;
   return (acc->type);
}

const char *
xaccAccountGetName (Account *acc)
{
   if (!acc) return NULL;
   return (acc->accountName);
}

char *
xaccAccountGetFullName(Account *account, const char separator)
{
  Account *a;
  char *fullname;
  const char *name;
  char *p;
  int length;

  if (account == NULL)
    return g_strdup("");

  /* Figure out how much space is needed */
  length = 0;
  a = account;
  while (a != NULL)
  {
    name = a->accountName;

    length += strlen(name) + 1; /* plus one for the separator */

    a = xaccAccountGetParentAccount(a);
  }

  /* length has one extra separator in it, that's ok, because it will
   * hold the null character at the end. */

  /* allocate the memory */
  fullname = g_new(char, length);

  /* go to end of string */
  p = fullname + length - 1;

  /* put in the null character and move to the previous char */
  *p-- = 0;

  a = account;
  while (a != NULL)
  {
    name = a->accountName;
    length = strlen(name);

    /* copy the characters going backwards */
    while (length > 0)
      *p-- = name[--length];

    a = xaccAccountGetParentAccount(a);

    /* if we're not at the root, add another separator */
    if (a != NULL)
      *p-- = separator;
  }

  return fullname;
}

const char *
xaccAccountGetCode (Account *acc)
{
   if (!acc) return NULL;
   return (acc->accountCode);
}

const char * 
xaccAccountGetDescription (Account *acc)
{
   if (!acc) return NULL;
   return (acc->description);
}

const char * 
xaccAccountGetNotes (Account *acc) 
{
  kvp_value *v;

  if (!acc) return NULL;
  v = kvp_frame_get_slot(acc->kvp_data, "notes");
  if(v) return(kvp_value_get_string(v));
  return(NULL);
}

gnc_commodity * 
DxaccAccountGetCurrency (Account *acc)
{
  kvp_value *v;
  const char *s;
  gnc_commodity_table *table;

  if (!acc) return NULL;

  v = kvp_frame_get_slot(acc->kvp_data, "old-currency");
  if (!v) return NULL;

  s = kvp_value_get_string (v);
  if (!s) return NULL;

  table = gnc_commodity_table_get_table (acc->book);

  return gnc_commodity_table_lookup_unique (table, s);
}

gnc_commodity * 
xaccAccountGetCommodity (Account *acc)
{
  if (!acc) return NULL;

  return (acc->commodity);
}

gnc_commodity *
DxaccAccountGetSecurity (Account *acc)
{
  kvp_value *v;
  const char *s;
  gnc_commodity_table *table;

  if (!acc) return NULL;

  v = kvp_frame_get_slot(acc->kvp_data, "old-security");
  if (!v) return NULL;

  s = kvp_value_get_string (v);
  if (!s) return NULL;

  table = gnc_commodity_table_get_table (acc->book);

  return gnc_commodity_table_lookup_unique (table, s);
}

gnc_numeric
xaccAccountGetBalance (Account *acc) 
{
  if (!acc) return gnc_numeric_zero();
  return acc->balance;
}

gnc_numeric
xaccAccountGetClearedBalance (Account *acc)
{
   if (!acc) return gnc_numeric_zero();
   return acc->cleared_balance;
}

gnc_numeric
xaccAccountGetReconciledBalance (Account *acc)
{
   if (!acc) return gnc_numeric_zero();
   return acc->reconciled_balance;
}

gnc_numeric
xaccAccountGetProjectedMinimumBalance (Account *account)
{
  GList *node;
  time_t today;
  gnc_numeric lowest = gnc_numeric_zero ();
  int seen_a_transaction = 0;

  if (!account)
    return gnc_numeric_zero ();

  today = gnc_timet_get_today_end();
  for (node = g_list_last (account->splits); node; node = node->prev)
  {
    Split *split = node->data;

    if (!seen_a_transaction)
    {
      lowest = xaccSplitGetBalance (split);
      seen_a_transaction = 1;
    } else if (gnc_numeric_compare(xaccSplitGetBalance (split), lowest) < 0) {
      lowest = xaccSplitGetBalance (split);
    }

    if (xaccTransGetDate (xaccSplitGetParent (split)) <= today)
      return lowest;
  }

  return lowest;
}


/********************************************************************\
\********************************************************************/

gnc_numeric
xaccAccountGetBalanceAsOfDate (Account *acc, time_t date)
{
  /* Ideally this could use xaccAccountForEachSplit, but
   * it doesn't exist yet and I'm uncertain of exactly how
   * it would work at this time, since it differs from
   * xaccAccountForEachTransaction by using gpointer return
   * values rather than gbooleans.
   */
  GList   *lp;
  Timespec ts, trans_ts;
  gboolean found = FALSE;
  gnc_numeric balance;

  if (!acc) return gnc_numeric_zero ();

  xaccAccountSortSplits (acc, TRUE); /* just in case, normally a noop */
  xaccAccountRecomputeBalance (acc); /* just in case, normally a noop */

  balance = acc->balance;

  /* Since transaction post times are stored as a Timespec,
   * convert date into a Timespec as well rather than converting
   * each transaction's Timespec into a time_t.
   */
  ts.tv_sec = date;
  ts.tv_nsec = 0;

  lp = acc->splits;
  while( lp && !found )
  {
    xaccTransGetDatePostedTS( xaccSplitGetParent( (Split *)lp->data ),
                              &trans_ts );
    if( timespec_cmp( &trans_ts, &ts ) > 0 )
      found = TRUE;
    else
      lp = lp->next;
  }

  if( lp && lp->prev )
  {
    /* Since lp is now pointing to a split which was past the reconcile
     * date, get the running balance of the previous split.
     */
    balance = xaccSplitGetBalance( (Split *)lp->prev->data );
  }

  /* Otherwise there were no splits posted after the given date,
   * so the latest account balance should be good enough.
   */

  return( balance );
}

/*
 * Originally gsr_account_present_balance in gnc-split-reg.c
 *
 * How does this routine compare to xaccAccountGetBalanceAsOfDate just
 * above?  These two routines should eventually be collapsed into one.
 * Perhaps the startup logic from that one, and the logic from this
 * one that walks from the tail of the split list.
 */
gnc_numeric
xaccAccountGetPresentBalance (Account *account)
{
  GList *node;
  time_t today;

  if (!account)
    return gnc_numeric_zero ();

  today = gnc_timet_get_today_end();
  for (node = g_list_last (account->splits); node; node = node->prev)
  {
    Split *split = node->data;

    if (xaccTransGetDate (xaccSplitGetParent (split)) <= today)
      return xaccSplitGetBalance (split);
  }

  return gnc_numeric_zero ();
}


/********************************************************************\
\********************************************************************/

/*
 * Convert a balance from one currency to another.
 */
gnc_numeric
xaccAccountConvertBalanceToCurrency(Account *account, /* for book */
				    gnc_numeric balance,
				    gnc_commodity *balance_currency,
				    gnc_commodity *new_currency)
{
  QofBook *book;
  GNCPriceDB *pdb;

  if (gnc_numeric_zero_p (balance) ||
      gnc_commodity_equiv (balance_currency, new_currency))
    return balance;

  book = xaccGroupGetBook (xaccAccountGetRoot (account));
  pdb = gnc_pricedb_get_db (book);

  balance = gnc_pricedb_convert_balance_latest_price(pdb, balance, balance_currency, new_currency);

  return balance;
}

/*
 * Given an account and a GetBalanceFn pointer, extract the requested
 * balance from the account and then convert it to the desired
 * currency.
 */
static gnc_numeric
xaccAccountGetXxxBalanceInCurrency (Account *account,
				    xaccGetBalanceFn fn,
				    gnc_commodity *report_currency)
{
  gnc_numeric balance;

  if (!account || !fn || !report_currency)
    return gnc_numeric_zero ();
  balance = fn(account);
  return xaccAccountConvertBalanceToCurrency(account, balance,
					     account->commodity,
					     report_currency);
}

/*
 * Data structure used to pass various arguments into the following fn.
 */
typedef struct
{
  gnc_commodity *currency;
  gnc_numeric balance;
  xaccGetBalanceFn fn;
} CurrencyBalance;


/*
 * A helper function for iterating over all the accounts in a list or
 * tree.  This function is called once per account, and sums up the
 * values of all these accounts.
 */
static gpointer
xaccAccountBalanceHelper (Account *account, gpointer data)
{
  CurrencyBalance *cb = data;
  gnc_numeric balance;

  if (!cb->fn || !cb->currency)
    return NULL;
  balance = xaccAccountGetXxxBalanceInCurrency (account, cb->fn, cb->currency);
  cb->balance = gnc_numeric_add (cb->balance, balance,
                                 gnc_commodity_get_fraction (cb->currency),
                                 GNC_RND_ROUND);
  return NULL;
}

/*
 * Common function that iterates recursively over all accounts below
 * the specified account.  It uses the previous routine to sum up the
 * balances of all its children, and uses the specified function for
 * extracting the balance.  This function may extract the current
 * value, the reconciled value, etc.
 */
static gnc_numeric
xaccAccountGetXxxBalanceInCurrencyRecursive (Account *account,
					     xaccGetBalanceFn fn,
					     gnc_commodity *report_commodity,
					     gboolean include_children)
{
  gnc_numeric balance;

  if (account == NULL)
    return gnc_numeric_zero ();
  if (!report_commodity)
    report_commodity = xaccAccountGetCommodity (account);
  balance = xaccAccountGetXxxBalanceInCurrency (account, fn, report_commodity);

  /* If needed, sum up the children converting to *this* account's commodity. */
  if (include_children)
  {
    CurrencyBalance cb = { report_commodity, balance, fn };

    xaccGroupForEachAccount (account->children, xaccAccountBalanceHelper, &cb, TRUE);
    balance = cb.balance;
  }

  return balance;
}

gnc_numeric
xaccAccountGetBalanceInCurrency (Account *account,
				 gnc_commodity *report_commodity,
				 gboolean include_children)
{
  return
    xaccAccountGetXxxBalanceInCurrencyRecursive (account, xaccAccountGetBalance,
						 report_commodity, include_children);
}


gnc_numeric
xaccAccountGetClearedBalanceInCurrency (Account *account,
					gnc_commodity *report_commodity,
					gboolean include_children)
{
  return
    xaccAccountGetXxxBalanceInCurrencyRecursive (account, xaccAccountGetClearedBalance,
						 report_commodity, include_children);
}


gnc_numeric
xaccAccountGetReconciledBalanceInCurrency (Account *account,
				 gnc_commodity *report_commodity,
				 gboolean include_children)
{
  return
    xaccAccountGetXxxBalanceInCurrencyRecursive (account, xaccAccountGetReconciledBalance,
						 report_commodity, include_children);
}

gnc_numeric
xaccAccountGetPresentBalanceInCurrency (Account *account,
					gnc_commodity *report_commodity,
					gboolean include_children)
{
  return
    xaccAccountGetXxxBalanceInCurrencyRecursive (account, xaccAccountGetPresentBalance,
						 report_commodity, include_children);
}

gnc_numeric
xaccAccountGetProjectedMinimumBalanceInCurrency (Account *account,
						 gnc_commodity *report_commodity,
						 gboolean include_children)
{
  return
    xaccAccountGetXxxBalanceInCurrencyRecursive (account, xaccAccountGetProjectedMinimumBalance,
						 report_commodity, include_children);
}

/********************************************************************\
\********************************************************************/

SplitList *
xaccAccountGetSplitList (Account *acc) 
{
  if (!acc) return NULL;
  return (acc->splits);
}

LotList *
xaccAccountGetLotList (Account *acc) 
{
  if (!acc) return NULL;
  return (acc->lots);
}

LotList *
xaccAccountFindOpenLots (Account *acc,
			gboolean (*match_func)(GNCLot *lot,
					       gpointer user_data),
			gpointer user_data, GCompareFunc sort_func)
{
  GList *lot_list;
  GList *retval = NULL;

  if (!acc)
    return NULL;

  lot_list = xaccAccountGetLotList (acc);
  for ( ; lot_list ; lot_list = lot_list->next ) {
    GNCLot *lot = lot_list->data;

    /* If this lot is closed, then ignore it */
    if (gnc_lot_is_closed (lot))
      continue;

    if (match_func && !(match_func)(lot, user_data))
      continue;

    /* Ok, this is a valid lot.  Add it to our list of lots */
    if (sort_func)
      retval = g_list_insert_sorted (retval, lot, sort_func);
    else
      retval = g_list_prepend (retval, lot);
  }

  return retval;
}

gpointer
xaccAccountForEachLot(Account *acc,
                              gpointer (*proc)(GNCLot *lot, void *data),
                              void *data) 
{
  LotList *node;

  if (!acc) return(NULL);
  if (!proc) return(NULL);
  
  for (node = acc->lots; node; node=node->next)
  {
    GNCLot *lot = node->data;
    gpointer result = proc (lot, data);
    if (result) return result;
  }
  
  return(NULL);
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountGetTaxRelated (Account *account)
{
  kvp_value *kvp;

  if (!account)
    return FALSE;

  kvp = kvp_frame_get_slot (account->kvp_data, "tax-related");
  if (!kvp)
    return FALSE;

  return kvp_value_get_gint64 (kvp);
}

void
xaccAccountSetTaxRelated (Account *account, gboolean tax_related)
{
  kvp_value *new_value;

  if (!account)
    return;

  if (tax_related)
    new_value = kvp_value_new_gint64 (tax_related);
  else
    new_value = NULL;

  xaccAccountBeginEdit (account);
  kvp_frame_set_slot_nc(account->kvp_data, "tax-related", new_value);

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

const char *
xaccAccountGetTaxUSCode (Account *account)
{
  kvp_value *value;

  if (!account)
    return FALSE;

  value = kvp_frame_get_slot_path (account->kvp_data, "tax-US", "code", NULL);
  if (!value)
    return NULL;

  return kvp_value_get_string (value);
}

void
xaccAccountSetTaxUSCode (Account *account, const char *code)
{
  kvp_frame *frame;

  if (!account)
    return;

  xaccAccountBeginEdit (account);

  frame = kvp_frame_get_frame (account->kvp_data, "tax-US", NULL);

  kvp_frame_set_slot_nc (frame, "code",
                         code ? kvp_value_new_string (code) : NULL);

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

const char *
xaccAccountGetTaxUSPayerNameSource (Account *account)
{
  kvp_value *value;

  if (!account)
    return FALSE;

  value = kvp_frame_get_slot_path (account->kvp_data,
                                   "tax-US", "payer-name-source", NULL);
  if (!value)
    return NULL;

  return kvp_value_get_string (value);
}

void
xaccAccountSetTaxUSPayerNameSource (Account *account, const char *source)
{
  kvp_frame *frame;

  if (!account)
    return;

  xaccAccountBeginEdit (account);

  frame = kvp_frame_get_frame (account->kvp_data, "tax-US", NULL);

  kvp_frame_set_slot_nc (frame, "payer-name-source",
                         source ? kvp_value_new_string (source) : NULL);

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountGetPlaceholder (Account *account)
{
  kvp_value *kvp;
  char *setting;

  if ( ( account )                                      &&
       ( kvp = kvp_frame_get_slot (account->kvp_data, "placeholder" ) ) &&
       ( kvp_value_get_type (kvp) == KVP_TYPE_STRING ) && 
       ( setting = kvp_value_get_string(kvp) ) &&
       ( !strcmp( setting, "true" ) ) )
      return TRUE;

  return FALSE;
}

void
xaccAccountSetPlaceholder (Account *account, gboolean option)
{
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  kvp_frame_set_slot_nc(account->kvp_data, "placeholder",
			kvp_value_new_string (option ? "true" : "false"));

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

GNCPlaceholderType
xaccAccountGetDescendantPlaceholder (Account *account)
{
  GList *descendants, *node;

  if (!account)
    return PLACEHOLDER_NONE;

  if (xaccAccountGetPlaceholder(account))
    return PLACEHOLDER_THIS;

  descendants = xaccGroupGetSubAccounts(account->children);
  node = g_list_first(descendants);
  for ( ; node ; node = g_list_next(node) ) 
  {
    account = (Account *)node->data;
    if (xaccAccountGetPlaceholder(account)) return(PLACEHOLDER_CHILD);
  }

  return PLACEHOLDER_NONE;
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountHasAncestor (Account *account, Account * ancestor)
{
  Account *parent;

  if ((account == NULL) || (ancestor == NULL))
    return FALSE;

  parent = xaccAccountGetParentAccount(account);
  while (parent != NULL)
  {
    if (parent == ancestor)
      return TRUE;

    parent = xaccAccountGetParentAccount(parent);
  }

  return FALSE;
}

/********************************************************************\
\********************************************************************/

/* You must edit the functions in this block in tandem.  KEEP THEM IN
   SYNC! */

#define GNC_RETURN_ENUM_AS_STRING(x) case (x): return #x;

char *
xaccAccountTypeEnumAsString(GNCAccountType type) 
{
  switch(type) 
  {
    GNC_RETURN_ENUM_AS_STRING(NO_TYPE);
    GNC_RETURN_ENUM_AS_STRING(BANK);
    GNC_RETURN_ENUM_AS_STRING(CASH);
    GNC_RETURN_ENUM_AS_STRING(CREDIT);
    GNC_RETURN_ENUM_AS_STRING(ASSET);
    GNC_RETURN_ENUM_AS_STRING(LIABILITY);
    GNC_RETURN_ENUM_AS_STRING(STOCK);
    GNC_RETURN_ENUM_AS_STRING(MUTUAL);
    GNC_RETURN_ENUM_AS_STRING(CURRENCY);
    GNC_RETURN_ENUM_AS_STRING(INCOME);
    GNC_RETURN_ENUM_AS_STRING(EXPENSE);
    GNC_RETURN_ENUM_AS_STRING(EQUITY);
    GNC_RETURN_ENUM_AS_STRING(RECEIVABLE);
    GNC_RETURN_ENUM_AS_STRING(PAYABLE);
    GNC_RETURN_ENUM_AS_STRING(CHECKING);
    GNC_RETURN_ENUM_AS_STRING(SAVINGS);
    GNC_RETURN_ENUM_AS_STRING(MONEYMRKT);
    GNC_RETURN_ENUM_AS_STRING(CREDITLINE);
    default:
      PERR ("asked to translate unknown account type %d.\n", type);
      break;
  }
  return(NULL);
}

#undef GNC_RETURN_ENUM_AS_STRING

#define GNC_RETURN_ON_MATCH(x) \
  if(safe_strcmp(#x, (str)) == 0) { *type = x; return(TRUE); }

gboolean
xaccAccountStringToType(const char* str, GNCAccountType *type)
{

  GNC_RETURN_ON_MATCH(NO_TYPE);
  GNC_RETURN_ON_MATCH(BANK);
  GNC_RETURN_ON_MATCH(CASH);
  GNC_RETURN_ON_MATCH(CREDIT);
  GNC_RETURN_ON_MATCH(ASSET);
  GNC_RETURN_ON_MATCH(LIABILITY);
  GNC_RETURN_ON_MATCH(STOCK);
  GNC_RETURN_ON_MATCH(MUTUAL);
  GNC_RETURN_ON_MATCH(CURRENCY);
  GNC_RETURN_ON_MATCH(INCOME);
  GNC_RETURN_ON_MATCH(EXPENSE);
  GNC_RETURN_ON_MATCH(EQUITY);
  GNC_RETURN_ON_MATCH(RECEIVABLE);
  GNC_RETURN_ON_MATCH(PAYABLE);
  GNC_RETURN_ON_MATCH(CHECKING);
  GNC_RETURN_ON_MATCH(SAVINGS);
  GNC_RETURN_ON_MATCH(MONEYMRKT);
  GNC_RETURN_ON_MATCH(CREDITLINE);

  PERR("asked to translate unknown account type string %s.\n",
       str ? str : "(null)");

  return(FALSE);
}

#undef GNC_RETURN_ON_MATCH

/* impedance mismatch is a source of loss */
GNCAccountType
xaccAccountStringToEnum(const char* str) 
{
  GNCAccountType type;
  gboolean rc;
  rc = xaccAccountStringToType(str, &type);
  if (FALSE == rc) return BAD_TYPE;
  return type;
}

/********************************************************************\
\********************************************************************/

static char *
account_type_name[NUM_ACCOUNT_TYPES] = { 
  N_("Bank"),
  N_("Cash"),
  N_("Asset"),
  N_("Credit Card"),
  N_("Liability"),
  N_("Stock"),
  N_("Mutual Fund"),
  N_("Currency"),
  N_("Income"),
  N_("Expense"),
  N_("Equity"),
  N_("A/Receivable"),
  N_("A/Payable")
  /*
    N_("Checking"),
    N_("Savings"),
    N_("Money Market"),
    N_("Credit Line")
  */
};

const char *
xaccAccountGetTypeStr(GNCAccountType type) {
  if (0 > type) return "";
  if (NUM_ACCOUNT_TYPES <= type) return "";
  return _(account_type_name [type]);
}

GNCAccountType
xaccAccountGetTypeFromStr (const gchar *str)
{
  gint type;

  for (type = 0; type < NUM_ACCOUNT_TYPES; type++)
  {
    if (!safe_strcmp (str, _(account_type_name [type])))
      return type;
  }

  PERR("asked to translate unknown account type string %s.\n",
       str ? str : "(null)");

  return BAD_TYPE;
}


/********************************************************************\
\********************************************************************/

gboolean
xaccAccountTypesCompatible (GNCAccountType parent_type,
                            GNCAccountType child_type)
{
  gboolean compatible = FALSE;

  switch(parent_type)
  {
    case BANK:
    case CASH: 
    case ASSET:
    case STOCK:
    case MUTUAL:
    case CURRENCY:
    case CREDIT:
    case LIABILITY:
    case RECEIVABLE:
    case PAYABLE:
      compatible = ((child_type == BANK)     ||
                    (child_type == CASH)     ||
                    (child_type == ASSET)    ||
                    (child_type == STOCK)    ||
                    (child_type == MUTUAL)   ||
                    (child_type == CURRENCY) ||
                    (child_type == CREDIT)   ||
                    (child_type == LIABILITY)||
                    (child_type == RECEIVABLE)||
                    (child_type == PAYABLE));
      break;
    case INCOME:
    case EXPENSE:
      compatible = ((child_type == INCOME) ||
                    (child_type == EXPENSE));
      break;
    case EQUITY:
      compatible = (child_type == EQUITY);
      break;
    default:
      PERR("bad account type: %d", parent_type);
      break;
  }

  return compatible;
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountGetReconcileLastDate (Account *account, time_t *last_date)
{
  kvp_value *value;

  if (!account)
    return FALSE;

  value = kvp_frame_get_slot_path (account->kvp_data,
                                   "reconcile-info", "last-date", NULL);
  if (!value)
    return FALSE;

  if (kvp_value_get_type (value) == KVP_TYPE_GINT64)
  {
    if (last_date)
      *last_date = kvp_value_get_gint64 (value);

    return TRUE;
  }

  return FALSE;
}

/********************************************************************\
\********************************************************************/

void
xaccAccountSetReconcileLastDate (Account *account, time_t last_date)
{
  kvp_frame *frame;
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  frame = kvp_frame_get_frame (account->kvp_data, "reconcile-info", NULL);
  kvp_frame_set_slot_nc (frame, "last-date", 
                               kvp_value_new_gint64 (last_date));

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountGetReconcileLastInterval (Account *account, int *months, int *days)
{
  kvp_value *value1, *value2;

  if (!account)
    return FALSE;

  value1 = kvp_frame_get_slot_path (account->kvp_data, "reconcile-info",
				    "last-interval", "months", NULL);
  value2 = kvp_frame_get_slot_path (account->kvp_data, "reconcile-info",
				    "last-interval", "days", NULL);
  if (!value1 || (kvp_value_get_type (value1) != KVP_TYPE_GINT64) ||
      !value2 || (kvp_value_get_type (value2) != KVP_TYPE_GINT64))
    return FALSE;

  if (months)
    *months = kvp_value_get_gint64 (value1);
  if (days)
    *days = kvp_value_get_gint64 (value2);
  return TRUE;
}

/********************************************************************\
\********************************************************************/

void
xaccAccountSetReconcileLastInterval (Account *account, int months, int days)
{
  kvp_frame *frame;
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  frame = kvp_frame_get_frame (account->kvp_data, "reconcile-info",
			       "last-interval", NULL);
  kvp_frame_set_slot_nc (frame, "months", 
                               kvp_value_new_gint64 (months));
  kvp_frame_set_slot_nc (frame, "days", 
                               kvp_value_new_gint64 (days));

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountGetReconcilePostponeDate (Account *account,
                                     time_t *postpone_date)
{
  kvp_value *value;

  if (!account)
    return FALSE;

  value = kvp_frame_get_slot_path (account->kvp_data,
                                   "reconcile-info", "postpone", "date", NULL);
  if (!value)
    return FALSE;

  if (kvp_value_get_type (value) == KVP_TYPE_GINT64)
  {
    if (postpone_date)
      *postpone_date = kvp_value_get_gint64 (value);

    return TRUE;
  }

  return FALSE;
}

/********************************************************************\
\********************************************************************/

void
xaccAccountSetReconcilePostponeDate (Account *account,
                                     time_t postpone_date)
{
  kvp_frame *frame;
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  frame = kvp_frame_get_frame (account->kvp_data, 
                         "reconcile-info", "postpone", NULL);

  kvp_frame_set_slot_nc (frame, "date", 
                         kvp_value_new_gint64 (postpone_date));

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountGetReconcilePostponeBalance (Account *account,
                                        gnc_numeric *balance)
{
  kvp_value *value;

  if (!account)
    return FALSE;

  value = kvp_frame_get_slot_path (account->kvp_data,
                                   "reconcile-info", "postpone", "balance",
                                   NULL);
  if (!value)
    return FALSE;

  if (kvp_value_get_type (value) == KVP_TYPE_NUMERIC)
  {
    if (balance)
      *balance = kvp_value_get_numeric (value);

    return TRUE;
  }

  return FALSE;
}

/********************************************************************\
\********************************************************************/

void
xaccAccountSetReconcilePostponeBalance (Account *account,
                                        gnc_numeric balance)
{
  kvp_frame *frame;
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  frame = kvp_frame_get_frame (account->kvp_data, 
                         "reconcile-info", "postpone", NULL);

  kvp_frame_set_slot_nc (frame, "balance", 
                         kvp_value_new_gnc_numeric (balance));

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\

\********************************************************************/

void
xaccAccountClearReconcilePostpone (Account *account)
{
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  {
    kvp_frame_set_slot_path (account->kvp_data, NULL,
                             "reconcile-info", "postpone", NULL);

    mark_account (account);
  }
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

/* xaccAccountGetAutoInterestXfer: determine whether the auto interest
 * xfer option is enabled for this account, and return that value.
 * If it is not defined for the account, return the default value.
 */
gboolean
xaccAccountGetAutoInterestXfer (Account *account, gboolean default_value)
{
  kvp_value *value = NULL;
  char *setting = NULL;
  gboolean result = default_value;

  if ( ( account )                                      &&
       ( value = kvp_frame_get_slot_path (account->kvp_data,
                                          "reconcile-info",
                                          "auto-interest-transfer",
                                          NULL) )        &&
       ( kvp_value_get_type (value) == KVP_TYPE_STRING ) && 
       ( setting = kvp_value_get_string(value) ) )
  {
    if( !strcmp( setting, "true" ) )
      result = TRUE;
    else if( !strcmp( setting, "false" ) )
      result = FALSE;
  }

  return (result);
}

/********************************************************************\
\********************************************************************/

void
xaccAccountSetAutoInterestXfer (Account *account, gboolean option)
{
  kvp_frame *frame;
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  frame = kvp_frame_get_frame (account->kvp_data, 
                         "reconcile-info", NULL);

  /* FIXME: need KVP_TYPE_BOOLEAN for this someday */

  kvp_frame_set_slot_nc (frame, "auto-interest-transfer", 
                         kvp_value_new_string (option ? "true" : "false"));

  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

const char *
xaccAccountGetLastNum (Account *account)
{
  kvp_value *value;

  if (!account)
    return FALSE;

  value = kvp_frame_get_slot (account->kvp_data, "last-num");
  if (!value)
    return FALSE;

  return kvp_value_get_string (value);
}

/********************************************************************\
\********************************************************************/

void
xaccAccountSetLastNum (Account *account, const char *num)
{
  if (!account)
    return;

  xaccAccountBeginEdit (account);
  kvp_frame_set_slot_nc (account->kvp_data, "last-num", 
                                            kvp_value_new_string (num));
  mark_account (account);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
}

/********************************************************************\
\********************************************************************/

void
dxaccAccountSetPriceSrc(Account *acc, const char *src)
{
  if(!acc) return;

  xaccAccountBeginEdit(acc);
  {
    GNCAccountType t = acc->type;

    if((t == STOCK) || (t == MUTUAL) || (t == CURRENCY)) {
      kvp_frame_set_slot_nc(acc->kvp_data,
                            "old-price-source",
                            src ? kvp_value_new_string(src) : NULL);
      mark_account (acc);
    }
  }
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

/********************************************************************\
\********************************************************************/

const char*
dxaccAccountGetPriceSrc(Account *acc) 
{
  GNCAccountType t;
  if(!acc) return NULL;

  t = acc->type;
  if((t == STOCK) || (t == MUTUAL) || (t == CURRENCY)) 
  {
    kvp_value *value = kvp_frame_get_slot(acc->kvp_data, "old-price-source");
    if(value) return (kvp_value_get_string(value));
  }
  return NULL;
}

/********************************************************************\
\********************************************************************/

void
dxaccAccountSetQuoteTZ(Account *acc, const char *tz) 
{
  if(!acc) return;

  xaccAccountBeginEdit(acc);
  {
    GNCAccountType t = acc->type;

    if((t == STOCK) || (t == MUTUAL) || (t == CURRENCY)) {
      kvp_frame_set_slot_nc(acc->kvp_data,
                            "old-quote-tz",
                            tz ? kvp_value_new_string(tz) : NULL);
      mark_account (acc);
    }
  }
  acc->core_dirty = TRUE;
  xaccAccountCommitEdit(acc);
}

/********************************************************************\
\********************************************************************/

const char*
dxaccAccountGetQuoteTZ(Account *acc) 
{
  GNCAccountType t;
  if(!acc) return NULL;

  t = acc->type;
  if((t == STOCK) || (t == MUTUAL) || (t == CURRENCY))
  {
    kvp_value *value = kvp_frame_get_slot(acc->kvp_data, "old-quote-tz");
    if(value) return (kvp_value_get_string(value));
  }
  return NULL;
}

/********************************************************************\
\********************************************************************/

void
xaccAccountSetReconcileChildrenStatus(Account *account, gboolean status)
{ 
  kvp_frame *frame;
  if (!account)
    return;
  
  xaccAccountBeginEdit (account);
  
  frame = kvp_frame_get_frame (account->kvp_data, "reconcile-info", NULL);
  kvp_frame_set_slot_nc (frame,
                         "include-children",
                         status ? kvp_value_new_gint64 (status) : NULL);
  account->core_dirty = TRUE;
  xaccAccountCommitEdit (account);
  return;
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountGetReconcileChildrenStatus(Account *account)
{
  kvp_value *status;
  if (!account)
    return FALSE;
  /* access the account's kvp-data for status and return that, if no value
   * is found then we can assume not to include the children, that being
   * the default behaviour 
   */
  status = kvp_frame_get_slot_path (account->kvp_data,
				    "reconcile-info",
				    "include-children",
				    NULL);
  if (!status)
    return FALSE;
  return kvp_value_get_gint64 (status);
}

/********************************************************************\
\********************************************************************/

gboolean
xaccAccountVisitUnvisitedTransactions(Account *acc,
                                      gboolean (*proc)(Transaction *t,
                                                       void *data),
                                      void *data,
                                      GHashTable *visited_txns) 
{
  gboolean keep_going = TRUE;
  GList *lp;

  if(!acc) return(FALSE);
  if(!proc) return(FALSE);
  if(!visited_txns) return(FALSE);

  for(lp = acc->splits; lp && keep_going; lp = lp->next) {
    Split *s = (Split *) lp->data;
    Transaction *t = xaccSplitGetParent(s);

    if(t) {
      const GUID *guid = xaccTransGetGUID(t);
      gpointer been_here = g_hash_table_lookup(visited_txns, guid);

      if(!GPOINTER_TO_INT(been_here)) {
        g_hash_table_insert(visited_txns, (gpointer) guid,
                            GINT_TO_POINTER(TRUE));
        if(!proc(t, data)) {
          keep_going = FALSE;
        }
      }
    }
  }
  return(keep_going);
}

gboolean
xaccAccountForEachTransaction(Account *acc,
                              gboolean (*proc)(Transaction *t, void *data),
                              void *data) 
{
  GHashTable *visited_txns = NULL;
  gboolean result = FALSE;

  if(!acc) return(FALSE);
  if(!proc) return(FALSE);
  
  visited_txns = guid_hash_table_new();
  if(visited_txns) {
    result =
      xaccAccountVisitUnvisitedTransactions(acc, proc, data, visited_txns);
  }
  
  /* cleanup */
  if(visited_txns) g_hash_table_destroy(visited_txns);  
  return(result);
}

/********************************************************************\
\********************************************************************/

/* The caller of this function can get back one or both of the
 * matching split and transaction pointers, depending on whether
 * a valid pointer to the location to store those pointers is
 * passed.
 */
static void
finder_help_function(Account *account,
                     const char *description,
                     Split **split,
                     Transaction **trans )
{
  GList *slp;

  /* First, make sure we set the data to NULL BEFORE we start */
  if (split) *split = NULL;
  if (trans) *trans = NULL;

  /* Then see if we have any work to do */
  if (account == NULL) return;

  /* Why is this loop iterated backwards ?? Presumably because the split
   * list is in date order, and the most recent matches should be 
   * returned!?  */
  for (slp = g_list_last (account->splits);
       slp;
       slp = slp->prev)
  {
    Split *lsplit = slp->data;
    Transaction *ltrans = xaccSplitGetParent(lsplit);

    if (safe_strcmp (description, xaccTransGetDescription (ltrans)) == 0)
    {
      if( split ) *split = lsplit;
      if( trans ) *trans = ltrans;
      return;
    }
  }
}

Split *
xaccAccountFindSplitByDesc(Account *account, const char *description)
{
  Split *split;

  /* Get the split which has a transaction matching the description. */
  finder_help_function(account, description, &split, NULL );

  return( split );
}

/* This routine is for finding a matching transaction in an account by
 * matching on the description field. This routine is used for auto-filling
 * in registers with a default leading account. The dest_trans is a
 * transaction used for currency checking. */
Transaction *
xaccAccountFindTransByDesc(Account *account, const char *description)
{
  Transaction *trans;

  /* Get the transation matching the description. */
  finder_help_function(account, description, NULL, &trans );

  return( trans );
}

/* ================================================================ */

Backend *
xaccAccountGetBackend (Account * acc)
{
  if (!acc || !acc->book) return NULL;
  return acc->book->backend;
}

/* ================================================================ */
/* gncObject function implementation and registration */

static void
account_foreach (QofBook *book, foreachObjectCB cb, gpointer ud)
{
  GNCEntityTable *et;

  g_return_if_fail (book);
  g_return_if_fail (cb);

  et = qof_book_get_entity_table (book);
  xaccForeachEntity (et, GNC_ID_ACCOUNT, cb, ud);
}

static QofObject account_object_def = {
  GNC_OBJECT_VERSION,
  GNC_ID_ACCOUNT,
  "Account",
  NULL,				/* book_begin */
  NULL,				/* book_end */
  NULL,				/* is_dirty */
  NULL,				/* mark_clean */
  account_foreach,		/* foreach */
  (const char* (*)(gpointer)) xaccAccountGetName /* printable */
};

gboolean xaccAccountRegister (void)
{
  static QofQueryObject params[] = {
    { ACCOUNT_KVP, QOF_QUERYCORE_KVP, (QofAccessFunc)xaccAccountGetSlots },
    { ACCOUNT_NAME_, QOF_QUERYCORE_STRING, (QofAccessFunc)xaccAccountGetName },
    { ACCOUNT_CODE_, QOF_QUERYCORE_STRING, (QofAccessFunc)xaccAccountGetCode },
    { ACCOUNT_DESCRIPTION_, QOF_QUERYCORE_STRING, (QofAccessFunc)xaccAccountGetDescription },
    { ACCOUNT_NOTES_, QOF_QUERYCORE_STRING, (QofAccessFunc)xaccAccountGetNotes },
    { ACCOUNT_PRESENT_, QOF_QUERYCORE_NUMERIC, (QofAccessFunc)xaccAccountGetPresentBalance },
    { ACCOUNT_BALANCE_, QOF_QUERYCORE_NUMERIC, (QofAccessFunc)xaccAccountGetBalance },
    { ACCOUNT_CLEARED_, QOF_QUERYCORE_NUMERIC, (QofAccessFunc)xaccAccountGetClearedBalance },
    { ACCOUNT_RECONCILED_, QOF_QUERYCORE_NUMERIC, (QofAccessFunc)xaccAccountGetReconciledBalance },
    { ACCOUNT_FUTURE_MINIMUM_, QOF_QUERYCORE_NUMERIC, (QofAccessFunc)xaccAccountGetProjectedMinimumBalance },
    { ACCOUNT_TAX_RELATED, QOF_QUERYCORE_BOOLEAN, (QofAccessFunc)xaccAccountGetTaxRelated },
    { QOF_QUERY_PARAM_BOOK, GNC_ID_BOOK, (QofAccessFunc)xaccAccountGetBook },
    { QOF_QUERY_PARAM_GUID, QOF_QUERYCORE_GUID, (QofAccessFunc)xaccAccountGetGUID },
    { NULL },
  };

  qof_query_object_register (GNC_ID_ACCOUNT, (QofSortFunc)xaccAccountOrder, params);

  return qof_object_register (&account_object_def);
}

/* ======================= END OF FILE =========================== */