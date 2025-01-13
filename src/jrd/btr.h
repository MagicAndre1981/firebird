/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		btr.h
 *	DESCRIPTION:	Index walking data structures
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 *
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "DecOSF" port
 *
 */

#ifndef JRD_BTR_H
#define JRD_BTR_H

#include "../jrd/constants.h"
#include "../common/classes/array.h"
#include "../include/fb_blk.h"

#include "../jrd/err_proto.h"    // Index error types
#include "../jrd/Resources.h"
#include "../jrd/RecordNumber.h"
#include "../jrd/sbm.h"
#include "../jrd/lck.h"
#include "../jrd/pag.h"

struct dsc;

namespace Jrd {

class jrd_rel;
class jrd_tra;
template <typename T> class vec;
class Statement;
struct temporary_key;
class thread_db;
class BtrPageGCLock;
class Sort;
class PartitionedSort;
struct sort_key_def;

enum class IdxCreate {AtOnce, ForRollback};

// Dependencies from/to foreign references

struct dep
{
	int dep_reference_id;
	int dep_relation;
	int dep_index;

	dep() = default;
};

// Primary dependencies from all foreign references to relation's
// primary/unique keys

typedef Firebird::HalfStaticArray<dep, 8> PrimaryDeps;

// Foreign references to other relations' primary/unique keys

typedef Firebird::HalfStaticArray<dep, 8> ForeignDeps;


// Index descriptor block -- used to hold info from index root page

struct index_desc
{
	ULONG	idx_root;						// Index root
	float	idx_selectivity;				// selectivity of index
	MetaId	idx_id;
	UCHAR	idx_flags;
	UCHAR	idx_runtime_flags;				// flags used at runtime, not stored on disk
	MetaId	idx_primary_index;				// id for primary key partner index
	MetaId	idx_primary_relation;			// id for primary key partner relation
	USHORT	idx_count;						// number of keys
	ForeignDeps*	idx_foreign_deps;		// foreign key partners
	ValueExprNode* idx_expression_node;		// node tree for indexed expression
	dsc		idx_expression_desc;			// descriptor for expression result
	Statement* idx_expression_statement;	// stored statement for expression evaluation
	BoolExprNode* idx_condition_node;		// node tree for index condition
	Statement* idx_condition_statement;		// stored statement for index condition
	float idx_fraction;						// fraction of keys included in the index
	// This structure should exactly match IRTD structure for current ODS
	struct idx_repeat
	{
		USHORT idx_field;					// field id
		USHORT idx_itype;					// data of field in index
		float idx_selectivity;				// segment selectivity
	} idx_rpt[MAX_INDEX_SEGMENTS];
};

typedef Firebird::HalfStaticArray<index_desc, 16> IndexDescList;

const USHORT idx_invalid = USHORT(~0);		// Applies to idx_id as special value

// index types and flags

// See jrd/intl.h for notes on idx_itype and dsc_sub_type considerations
// idx_numeric .. idx_byte_array values are compatible with VMS values

const int idx_numeric		= 0;
const int idx_string		= 1;
// value of 2 was used in ODS < 10
const int idx_byte_array	= 3;
const int idx_metadata		= 4;
const int idx_sql_date		= 5;
const int idx_sql_time		= 6;
const int idx_timestamp		= 7;
const int idx_numeric2		= 8;	// Introduced for 64-bit Integer support
const int idx_boolean		= 9;
const int idx_decimal		= 10;
const int idx_sql_time_tz	= 11;
const int idx_timestamp_tz	= 12;
const int idx_bcd			= 13;	// 128-bit Integer support

// idx_itype space for future expansion
const int idx_first_intl_string	= 64;	// .. MAX (short) Range of computed key strings

const int idx_offset_intl_range	= (0x7FFF + idx_first_intl_string);

// these flags must match the irt_flags (see ods.h)

const UCHAR idx_unique		= 1;
const UCHAR idx_descending	= 2;
//const UCHAR idx_state_a	= 4;
const UCHAR idx_foreign		= 8;
const UCHAR idx_primary		= 16;
const UCHAR idx_expression	= 32;
const UCHAR idx_condition	= 64;
//const UCHAR idx_state_b	= 128;

// these flags are for idx_runtime_flags

const int idx_plan_dont_use	= 1;	// index is not mentioned in user-specified access plan
const int idx_plan_navigate	= 2;	// plan specifies index to be used for ordering
const int idx_used 			= 4;	// index was in fact selected for retrieval
const int idx_navigate		= 8;	// index was in fact selected for navigation
const int idx_marker		= 16;	// marker used in procedure sort_indices

// Index insertion block -- parameter block for index insertions

struct index_insertion
{
	RecordNumber iib_number;		// record number (or lower level page)
	ULONG iib_sibling;				// right sibling page
	index_desc*	iib_descriptor;		// index descriptor
	jrd_rel*	iib_relation;		// relation block
	temporary_key*	iib_key;		// varying string for insertion
	RecordBitmap* iib_duplicates;	// spare bit map of duplicates
	jrd_tra*	iib_transaction;	// insertion transaction
	BtrPageGCLock*	iib_dont_gc_lock;	// lock to prevent removal of splitted page
	UCHAR	iib_btr_level;			// target level to propagate split page to
};


// these flags are for the key_flags

const int key_empty		= 1;	// Key contains empty data / empty string

// Temporary key block

struct temporary_key
{
	USHORT key_length;
	UCHAR key_data[MAX_KEY + 1];
	UCHAR key_flags;
	USHORT key_nulls;	// bitmap of encountered null segments,
						// USHORT is enough to store MAX_INDEX_SEGMENTS bits
	Firebird::AutoPtr<temporary_key> key_next;	// next key (INTL_KEY_MULTI_STARTING)
};


// Index Sort Record -- fix part of sort record for index fast load

// hvlad: index_sort_record structure is stored in sort scratch file so we
// don't want to grow sort file with padding added by compiler to please
// alignment rules.
#pragma pack(1)
struct index_sort_record
{
	// RecordNumber should be at the first place, because it's used
	// for determing sort by creating index (see idx.cpp)
	SINT64 isr_record_number;
	USHORT isr_key_length;
	USHORT isr_flags;
};
#pragma pack()

const int ISR_secondary	= 1;	// Record is secondary version
const int ISR_null		= 2;	// Record consists of NULL values only



// Index retrieval block -- hold stuff for index retrieval

class IndexRetrieval
{
public:
	IndexRetrieval(jrd_rel* relation, const index_desc* idx, USHORT count, temporary_key* key)
		: irb_rsc_relation(), irb_jrd_relation(relation), irb_index(idx->idx_id),
		  irb_generic(0), irb_lower_count(count), irb_upper_count(count), irb_key(key),
		  irb_name(NULL), irb_value(NULL)
	{
		memcpy(&irb_desc, idx, sizeof(irb_desc));
	}

	IndexRetrieval(MemoryPool& pool, Rsc::Rel relation, const index_desc* idx,
				   const MetaName& name)
		: irb_rsc_relation(relation), irb_jrd_relation(nullptr), irb_index(idx->idx_id),
		  irb_generic(0), irb_lower_count(0), irb_upper_count(0), irb_key(NULL),
		  irb_name(FB_NEW_POOL(pool) MetaName(name)),
		  irb_value(FB_NEW_POOL(pool) ValueExprNode*[idx->idx_count * 2])
	{
		memcpy(&irb_desc, idx, sizeof(irb_desc));
	}

	~IndexRetrieval()
	{
		delete irb_name;
		delete[] irb_value;
	}

	jrd_rel* getRelation(thread_db* tdbb) const;
	Cached::Relation* getPermRelation() const;

	index_desc irb_desc;			// Index descriptor

private:
	Rsc::Rel irb_rsc_relation;		// Relation for retrieval
	jrd_rel* irb_jrd_relation;		// when used in different contexts

public:
	USHORT irb_index;				// Index id
	USHORT irb_generic;				// Flags for generic search
	USHORT irb_lower_count;			// Number of segments for retrieval
	USHORT irb_upper_count;			// Number of segments for retrieval
	temporary_key* irb_key;			// Key for equality retrieval
	MetaName* irb_name;	// Index name
	ValueExprNode** irb_value;
};

// Flag values for irb_generic
const int irb_partial	= 1;				// Partial match: not all segments or starting of key only
const int irb_starting	= 2;				// Only compute "starting with" key for index segment
const int irb_equality	= 4;				// Probing index for equality match
const int irb_ignore_null_value_key  = 8;	// if lower bound is specified and upper bound unspecified,
											// ignore looking at null value keys
const int irb_descending	= 16;			// Base index uses descending order
const int irb_exclude_lower	= 32;			// exclude lower bound keys while scanning index
const int irb_exclude_upper	= 64;			// exclude upper bound keys while scanning index
const int irb_multi_starting	= 128;		// Use INTL_KEY_MULTI_STARTING

typedef Firebird::HalfStaticArray<float, 4> SelectivityList;

class BtrPageGCLock : public Lock
{
	// This class assumes that the static part of the lock key (Lock::lck_key)
	// is at least 64 bits in size

public:
	explicit BtrPageGCLock(thread_db* tdbb);
	~BtrPageGCLock();

	void disablePageGC(thread_db* tdbb, const PageNumber &page);
	void enablePageGC(thread_db* tdbb);

	// return true if lock is active
	bool isActive() const
	{
		return lck_id != 0;
	}

	static bool isPageGCAllowed(thread_db* tdbb, const PageNumber& page);

#ifdef DEBUG_LCK_LIST
	BtrPageGCLock(thread_db* tdbb, Firebird::MemoryPool* pool)
		: Lock(tdbb, PageNumber::getLockLen(), LCK_btr_dont_gc), m_pool(pool)
	{
	}

	static bool checkPool(const Lock* lock, Firebird::MemoryPool* pool)
	{
		if (!pool || !lock)
			return false;

		const Firebird::MemoryPool* pool2 = NULL;

		if (lock && (lock->lck_type == LCK_btr_dont_gc))
			pool2 = reinterpret_cast<const BtrPageGCLock*>(lock)->m_pool;

		return (pool == pool2);
	}

private:
	const Firebird::MemoryPool* m_pool;
#endif
};

// Struct used for index creation

struct IndexCreation
{
	jrd_rel* relation;
	index_desc* index;
	const TEXT* index_name;
	jrd_tra* transaction;
	PartitionedSort* sort;
	sort_key_def* key_desc;
	USHORT key_length;
	USHORT nullIndLen;
	SINT64 dup_recno;
	Firebird::AtomicCounter duplicates;
	IdxCreate forRollback;
};

// Class used to report any index related errors

class IndexErrorContext
{
	struct Location
	{
		jrd_rel* relation;
		USHORT indexId;
	};

public:
	IndexErrorContext(jrd_rel* relation, index_desc* index, const char* indexName = NULL)
		: m_relation(relation), m_index(index), m_indexName(indexName), isLocationDefined(false)
	{}

	void setErrorLocation(jrd_rel* relation, USHORT indexId)
	{
		isLocationDefined = true;
		m_location.relation = relation;
		m_location.indexId = indexId;
	}

	void raise(thread_db*, idx_e, Record*);

private:
	jrd_rel* const m_relation;
	index_desc* const m_index;
	const char* const m_indexName;
	Location m_location;
	bool isLocationDefined;
};

class IndexCreateLock : public Firebird::AutoStorage
{
public:
	IndexCreateLock(thread_db* tdbb, MetaId relId);
	~IndexCreateLock();

	void exclusive(MetaId indexId);
	void shared(MetaId indexId);

private:
	thread_db* tdbb;	// may be stored here cause IndexCreateLock is always on stack
	MetaId relId;
	Lock* lck = nullptr;

	void makeLock(MetaId indexId);
};

} //namespace Jrd

#endif // JRD_BTR_H
