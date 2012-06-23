#include "moarvm.h"
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);

/* Garbage collects the nursery. This is a semi-space copying collector,
 * but only copies very young objects. Once an object is seen/copied once
 * in here (may be tuned in the future to twice or so - we'll see) then it
 * is not copied to tospace, but instead promoted to the second generation,
 * which is managed through mark-compact. Note that it adds the roots and
 * processes them in phases, to try to avoid building up a huge worklist. */
void MVM_gc_nursery_collect(MVMThreadContext *tc) {
    MVMGCWorklist *worklist;
    void *fromspace;
    void *tospace;
    
    /* Swap fromspace and tospace. */
    fromspace = tc->nursery_tospace;
    tospace   = tc->nursery_fromspace;
    tc->nursery_fromspace = fromspace;
    tc->nursery_tospace   = tospace;
    
    /* Reset nursery allocation pointers to the new tospace. */
    tc->nursery_alloc       = tospace;
    tc->nursery_alloc_limit = (char *)tc->nursery_alloc + MVM_NURSERY_SIZE;
    
    /* Create a GC worklist. */
    worklist = MVM_gc_worklist_create(tc);
    
    /* Add permanent roots and process them. */
    MVM_gc_root_add_parmanents_to_worklist(tc, worklist);
    process_worklist(tc, worklist);
    
    /* Add temporary roots and process them. */
    
    /* Add things that are roots for the first generation because
     * they are pointed to by objects in the second generation and
     * process them. */
    
    /* Find roots in frames and process them. */
    
    /* Destroy the worklist. */
    MVM_gc_worklist_destroy(tc, worklist);
}

/* Processes the current worklist. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMCollectable **item_ptr;
    MVMuint16        i;
    
    while (item_ptr = MVM_gc_worklist_get(tc, worklist)) {
        /* Dereference the object we're considering. */
        MVMCollectable *item = *item_ptr;

        /* If the item is NULL, that's fine - it's just a null reference and
         * thus we've no object to consider. */
        if (item == NULL)
            continue;

        /* If it's in the second generation, we have nothing to do. */
        if (item->flags & MVM_CF_SECOND_GEN)
            continue;
        
        /* If we already saw the item and copied it, then it will have a
         * forwarding address already. Just update this pointer to the
         * new address and we're done. */
        if (item->forwarder) {
            *item_ptr = item->forwarder;
            continue;
        }
        
        /* If we saw it in the nursery before, then we will promote it
         * to the second generation. */
        if (item->flags & MVM_CF_NURSERY_SEEN) {
            MVM_panic(15, "Promotion to second generation is NYI!");
            continue;
        }
        
        /* Otherwise, we need to do the copy. What sort of thing are we
         * going to copy? */
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC))) {
            /* It's an object instance. Get the size from the STable. */
            MVMuint32 size = ((MVMObject *)item)->st->size;
            
            /* Determine the new address and allocate space. */
            MVMObject *new_addr = (MVMObject *)tc->nursery_alloc;
            tc->nursery_alloc = (char *)tc->nursery_alloc + size;
            
            /* Copy the object to tospace and mark it as seen in the
             * nursery (so the next time around it will move to the
             * older generation). */
            memcpy(new_addr, item, size);
            new_addr->header.flags |= MVM_CF_NURSERY_SEEN;
            
            /* Store the forwarding pointer and update the original
             * reference. */
            *item_ptr = item->forwarder = (MVMCollectable *)new_addr;
            
            /* Add the serialization context and STable to the worklist. */
            MVM_gc_worklist_add(tc, worklist, &new_addr->header.sc);
            MVM_gc_worklist_add(tc, worklist, &new_addr->st);
            
            /* If needed, mark it. This will add addresses to the worklist
             * that will need updating. Note that we are passing the address
             * of the object *after* copying it since those are the addresses
             * we care about updating; the old chunk of memory is now dead! */
            if (REPR(new_addr)->gc_mark)
                REPR(new_addr)->gc_mark(tc, STABLE(new_addr), OBJECT_BODY(new_addr), worklist);
        }
        else if (item->flags & MVM_CF_TYPE_OBJECT) {
            /* Determine the new address and allocate space. */
            MVMObject *new_addr = (MVMObject *)tc->nursery_alloc;
            tc->nursery_alloc = (char *)tc->nursery_alloc + sizeof(MVMObject);
            
            /* Copy the type object to tospace and mark it as seen in
             * the nursery (so the next time around it will move to
             * the older generation). */
            memcpy(new_addr, item, sizeof(MVMObject));
            new_addr->header.flags |= MVM_CF_NURSERY_SEEN;
            
            /* Store the forwarding pointer and update the original
             * reference. */
            *item_ptr = item->forwarder = (MVMCollectable *)new_addr;
            
            /* Add the serialization context and STable to the worklist. */
            MVM_gc_worklist_add(tc, worklist, &new_addr->header.sc);
            MVM_gc_worklist_add(tc, worklist, &new_addr->st);
        }
        else if (item->flags & MVM_CF_STABLE) {
            /* Determine the new address and allocate space. */
            MVMSTable *new_addr = (MVMSTable *)tc->nursery_alloc;
            tc->nursery_alloc = (char *)tc->nursery_alloc + sizeof(MVMSTable);
            
            /* Copy the type object to tospace and mark it as seen in
             * the nursery (so the next time around it will move to
             * the older generation). */
            memcpy(new_addr, item, sizeof(MVMSTable));
            new_addr->header.flags |= MVM_CF_NURSERY_SEEN;
            
            /* Store the forwarding pointer and update the original
             * reference. */
            *item_ptr = item->forwarder = (MVMCollectable *)new_addr;
            
            /* Add all references in the STable to the work list. */
            MVM_gc_worklist_add(tc, worklist, &new_addr->header.sc);
            MVM_gc_worklist_add(tc, worklist, &new_addr->HOW);
            MVM_gc_worklist_add(tc, worklist, &new_addr->WHAT);
            MVM_gc_worklist_add(tc, worklist, &new_addr->method_cache);
            for (i = 0; i < new_addr->vtable_length; i++)
                MVM_gc_worklist_add(tc, worklist, &new_addr->vtable[i]);
            for (i = 0; i < new_addr->type_check_cache_length; i++)
                MVM_gc_worklist_add(tc, worklist, &new_addr->type_check_cache[i]);
            if (new_addr->container_spec) {
                MVM_gc_worklist_add(tc, worklist, &new_addr->container_spec->value_slot.class_handle);
                MVM_gc_worklist_add(tc, worklist, &new_addr->container_spec->value_slot.attr_name);
                MVM_gc_worklist_add(tc, worklist, &new_addr->container_spec->fetch_method);
            }
            if (new_addr->boolification_spec)
                MVM_gc_worklist_add(tc, worklist, &new_addr->boolification_spec->method);
            MVM_gc_worklist_add(tc, worklist, &new_addr->WHO);
            
            /* If it needs to have it's REPR data marked, do that. */
            if (new_addr->REPR->gc_mark_repr_data)
                new_addr->REPR->gc_mark_repr_data(tc, new_addr, worklist);
        }
        else if (item->flags & MVM_CF_SC) {
            MVM_panic(15, "Can't copy serialization contexts in the GC yet");
        }
        else {
            MVM_panic(15, "Internal error: impossible case encountered in GC copy");
        }
    }
}

/* Some objects, having been copied, need no further attention. Others
 * need to do some additional freeing, however. This goes through the
 * fromspace and does any needed work to free uncopied things (this may
 * run in parallel with the mutator, which will be operating on tospace). */
void MVM_gc_nursery_free_uncopied(MVMThreadContext *tc, void *limit) {
}
