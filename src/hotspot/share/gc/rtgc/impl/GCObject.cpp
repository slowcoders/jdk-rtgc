#include "precompiled.hpp"
#include "oops/klass.inline.hpp"

#include "GCObject.hpp"
#include "GCRuntime.hpp"
#include "../rtgcGlobals.hpp"

using namespace RTGC;

void* GCNode::g_trackable_heap_start = 0;

static const int LOG_OPT(int function) {
  return LOG_OPTION(RTGC::LOG_GCNODE, function);
}

static void assert_valid_link(oopDesc* link, oopDesc* anchor) {
    assert(link != anchor ||
        (rtHeap::in_full_gc && link->is_gc_marked() && 
         link->forwardee() != NULL && (void*)link->forwardee() != anchor), 
            "recursive link %p\n", link);
}

ReferrerList* NodeInfo::getAnchorList() {
    precond(_hasMultiRef);
    return ReferrerList::getPointer(_refs);
}

void MutableNode::setAnchorList(ReferrerList* anchors) {
    precond(!_hasMultiRef);
    _refs = ReferrerList::getIndex(anchors);
    _hasMultiRef = true;
	debug_only(_isModified = true;)
}

GCObject* NodeInfo::getSafeAnchor() {
    assert(isAnchored(), "no anchors %p(%s)\n", this, RTGC::getClassName(this)); 
    GCObject* front;
    if (hasMultiRef()) {
        ReferrerList* referrers = getAnchorList();
        front = referrers->front();
    }
    else {
        front = getSingleAnchor();
    }
    return front;
}

void MutableNode::setSafeAnchor(GCObject* anchor) {
    // assert(isAnchored() && SafeShortcut::isValidIndex(this->getShortcutId()), 
    //     "incorrect anchor(%p) for empty obj(%p:%d)", anchor, this, this->getShortcutId());
    precond(!this->getShortcut()->isValid());

    if (hasMultiRef()) {
        ReferrerList* referrers = getAnchorList();
        referrers->replaceFirst(anchor);
    }
    else {
        assert(getSingleAnchor() == anchor, "incorrect safe anchor(%p) for this(%p). it must be (%p)",
            anchor, this, (GCObject*)getSingleAnchor());
    }
    // this->setShortcutId_unsafe(INVALID_SHORTCUT);
}

SafeShortcut* NodeInfo::getShortcut() {
    int p_id = this->getShortcutId();
    return SafeShortcut::getPointer(p_id);
}


int GCNode::getReferrerCount() {
    NodeInfo nx = this->getNodeInfo();
    if (!nx.isAnchored()) return 0;
    if (!nx.hasMultiRef()) return 1;
    ReferrerList* referrers = nx.getAnchorList();
    return referrers->approximated_item_count();
}

void GCObject::addReferrer(GCObject* referrer) {
    /**
     * 주의!) referrer 는 아직, memory 내용이 복사되지 않은 주소일 수 있다.
     */
    // rtgc_debug_log(this, "referrer %p added to %p\n", referrer, this);
    assert_valid_link(cast_to_oop(this), cast_to_oop(referrer));
    NodeInfo rt(this);
    if (!rt.isAnchored()) {
        MutableNode nx(this, rt);
        nx.setSingleAnchor(referrer);
        postcond(referrer == nx.getSingleAnchor());
    }
    else {
        ReferrerList* referrers;
        if (!rt.hasMultiRef()) {
            referrers = ReferrerList::allocate();
            referrers->init(rt.getSingleAnchor(), referrer);
            MutableNode nx(this, rt);
            nx.setAnchorList(referrers);
        }
        else {
            referrers = rt.getAnchorList();
            referrers->push_back(referrer);
        }
    }
}

bool GCObject::hasReferrer(GCObject* referrer) {
    NodeInfo nx = getNodeInfo();
    if (!nx.isAnchored()) return false;

    if (!nx.hasMultiRef()) {
        return nx.getSingleAnchor() == referrer;
    }
    else {
        ReferrerList* referrers = nx.getAnchorList();
        return referrers->contains(referrer);
    }
}


template <bool reallocReferrerList, bool must_exist, bool remove_mutiple_items>
int  GCObject::removeReferrer_impl(GCObject* referrer) {
    precond(referrer != this);

    NodeInfo rt(this);
    if (!must_exist && !rt.isAnchored()) return -1;

    assert(rt.isAnchored(), "no referrer %p(%s) in empty %p(%s) \n", 
        referrer, RTGC::getClassName(referrer, true),
        this, RTGC::getClassName(this));
    rtgc_debug_log(this, "removing anchor %p(%s)(gc_m=%d) from " PTR_DBG_SIG, 
            referrer, RTGC::getClassName(referrer), 
            cast_to_oop(referrer)->is_gc_marked(), PTR_DBG_INFO(this)); 

    if (!rt.hasMultiRef()) {
        if (!must_exist && rt.getSingleAnchor() != referrer) return -1;

        assert(rt.getSingleAnchor() == referrer, 
            "referrer %p(%s) != %p in %p(%s) \n", 
            referrer, RTGC::getClassName(referrer),
            (GCObject*)rt.getSingleAnchor(),
            this, RTGC::getClassName(this));
        MutableNode nx(this, rt);
        nx.removeSingleAnchor();
        rt = nx;
        rtgc_debug_log(this, "anchor-list cleared by removeReferrer %p\n", this);
    }
    else {
        ReferrerList* referrers = rt.getAnchorList();
        const void* removed;
        if (remove_mutiple_items) {
            removed = referrers->removeMatchedItems(referrer);
        } else {
            removed = referrers->remove(referrer);
        }
        rtgc_debug_log(this, "anchor removed(%p/%d) from " PTR_DBG_SIG, 
            removed, removed == referrers->firstItemPtr(), PTR_DBG_INFO(this)); 

        if (!must_exist && removed == NULL) return -1;

#ifdef ASSERT            
        if (removed == NULL) {
            for (AnchorIterator it(this); it.hasNext(); ) {
                GCObject* anchor = it.next();
                rtgc_log(1, "at %p(%s)\n", anchor, RTGC::getClassName(anchor));
            }
        }
#endif        
        assert(removed != NULL, "referrer %p(%s) is not found in %p(%s) \n", 
            referrer, RTGC::getClassName(referrer),
            this, RTGC::getClassName(this));

        bool first_item_removed = removed == referrers->firstItemPtr();
        MutableNode nx(this, rt);
        if (reallocReferrerList && referrers->isTooSmall()) {
            nx.setHasMultiRef(false);
            if (referrers->empty()) {
                nx.removeSingleAnchor();
            } else {
                nx.setSingleAnchor(referrers->front());
            }
            ReferrerList::delete_(referrers);
        } else {
            postcond(!reallocReferrerList || !referrers->empty());
        }

        if (!first_item_removed) {
            return +1;
        }
        rt = nx;
    }

    if (!rt.hasSafeAnchor()) {
        return rt.isAnchored() ? 1 : 0;
    }

    if (rt.hasShortcut()) {
		SafeShortcut* shortcut = rt.getShortcut();
        shortcut->split(referrer, this);
    } 
    else if (rt.hasSafeAnchor()) {
        MutableNode nx(this, rt);
        nx.invalidateSafeAnchor();
    }

    return 0;
}

void GCObject::removeReferrer(GCObject* referrer) {
    if (removeReferrer_impl<true, true, false>(referrer) == 0) {
        GCRuntime::detectUnsafeObject(this);
    }
}

void GCObject::removeReferrerWithoutReallocaton(GCObject* referrer) {
    if (removeReferrer_impl<false, true, false>(referrer) == 0) {
        // GCRuntime::detectUnsafeObject(this);
    }
}

bool GCObject::tryRemoveReferrer(GCObject* referrer) {
    int res = removeReferrer_impl<true, false, false>(referrer);
    if (res == 0) {
        GCRuntime::detectUnsafeObject(this);
    }
    return res != -1;
}

bool GCObject::removeMatchedReferrers(GCObject* referrer) {
    return removeReferrer_impl<true, false, true>(referrer) == 0;
}

bool GCObject::containsReferrer(GCObject* referrer) {
    precond(referrer != this);
    NodeInfo nx = getNodeInfo();
    if (!nx.isAnchored()) return false;

    if (!nx.hasMultiRef()) {
        return nx.getSingleAnchor() == referrer;
    }
    else {
        ReferrerList* referrers = nx.getAnchorList();
        return referrers->contains(referrer);
    }
}

void GCObject::invalidateAnchorList_unsafe() {
    MutableNode nx(this);
    nx.setHasMultiRef(false);
	nx.removeSingleAnchor();
}

void GCObject::clearAnchorList() {
    rtgc_debug_log(this, "all anchor removed from " PTR_DBG_SIG, PTR_DBG_INFO(this));
    MutableNode nx(this);
    precond(!nx.hasShortcut());
    if (nx.hasMultiRef()) {
        ReferrerList* referrers = nx.getAnchorList();
        ReferrerList::delete_(referrers);
        nx.setHasMultiRef(false);
    }
    nx.removeSingleAnchor();    
}

bool GCObject::clearEmptyAnchorList() {
    NodeInfo rt(this);
    if (rt.hasMultiRef()) {
        ReferrerList* referrers = rt.getAnchorList();
        if (referrers->isTooSmall()) {
            MutableNode nx(this, rt);
            precond(!nx.hasShortcut());
            nx.setHasMultiRef(false);
            if (referrers->empty()) {
                nx.removeSingleAnchor();
            } else {
                nx.setSingleAnchor(referrers->front());
            }
            ReferrerList::delete_(referrers);
        }
    }    
    return !nx.isAnchored();
}

void GCObject::removeAllAnchors() {
    rtgc_log(LOG_OPT(1), "refList of garbage cleaned %p\n", this);

    NodeInfo nx = getNodeInfo();
    if (nx.hasShortcut()) {
        SafeShortcut* shortcut = nx.getShortcut();
        shortcut->shrinkTailTo(nx.getSafeAnchor());
    }  
    clearAnchorList();
    rtgc_debug_log(this, "anchor-list cleared by removeAllAnchors %p\n", this);
}



void GCObject::invaliateSurvivalPath(GCObject* newTail) {
    NodeInfo rt = getNodeInfo();
	if (rt.getShortcutId() > 0) {
		SafeShortcut* shortcut = rt.getShortcut();
        shortcut->split(newTail, this);
        MutableNode nx(this, rt);
    	nx.setShortcutId_unsafe(0);
	}
}

