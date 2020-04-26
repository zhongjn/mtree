/*-------------------------------------------------------------------------
 *
 * mtreevalidate.c
 *	  Opclass validator for MTree.
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/access/mtree/mtreevalidate.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/amvalidate.h"
#include "mtree_private.h"
#include "access/htup_details.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/regproc.h"
#include "utils/syscache.h"


/*
 * Validator for a MTree opclass.
 */
bool
mtreevalidate(Oid opclassoid)
{
	bool		result = true;
	HeapTuple	classtup;
	Form_pg_opclass classform;
	Oid			opfamilyoid;
	Oid			opcintype;
	Oid			opckeytype;
	char	   *opclassname;
	HeapTuple	familytup;
	Form_pg_opfamily familyform;
	char	   *opfamilyname;
	CatCList   *proclist,
			   *oprlist;
	List	   *grouplist;
	OpFamilyOpFuncGroup *opclassgroup;
	int			i;
	ListCell   *lc;

	/* Fetch opclass information */
	classtup = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclassoid));
	if (!HeapTupleIsValid(classtup))
		elog(ERROR, "cache lookup failed for operator class %u", opclassoid);
	classform = (Form_pg_opclass) GETSTRUCT(classtup);

	opfamilyoid = classform->opcfamily;
	opcintype = classform->opcintype;
	opckeytype = classform->opckeytype;
	if (!OidIsValid(opckeytype))
		opckeytype = opcintype;
	opclassname = NameStr(classform->opcname);

	/* Fetch opfamily information */
	familytup = SearchSysCache1(OPFAMILYOID, ObjectIdGetDatum(opfamilyoid));
	if (!HeapTupleIsValid(familytup))
		elog(ERROR, "cache lookup failed for operator family %u", opfamilyoid);
	familyform = (Form_pg_opfamily) GETSTRUCT(familytup);

	opfamilyname = NameStr(familyform->opfname);

	/* Fetch all operators and support functions of the opfamily */
	oprlist = SearchSysCacheList1(AMOPSTRATEGY, ObjectIdGetDatum(opfamilyoid));
	proclist = SearchSysCacheList1(AMPROCNUM, ObjectIdGetDatum(opfamilyoid));

	/* Check individual support functions */
	for (i = 0; i < proclist->n_members; i++)
	{
		HeapTuple	proctup = &proclist->members[i]->tuple;
		Form_pg_amproc procform = (Form_pg_amproc) GETSTRUCT(proctup);
		bool		ok;

		/*
		 * All MTree support functions should be remtreeered with matching
		 * left/right types
		 */
		if (procform->amproclefttype != procform->amprocrighttype)
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("operator family \"%s\" of access method %s contains support function %s with different left and right input types",
							opfamilyname, "mtree",
							format_procedure(procform->amproc))));
			result = false;
		}

		/*
		 * We can't check signatures except within the specific opclass, since
		 * we need to know the associated opckeytype in many cases.
		 */
		if (procform->amproclefttype != opcintype)
			continue;

		/* Check procedure numbers and function signatures */
		switch (procform->amprocnum)
		{
			case MTREE_EQUAL_PROC:
				ok = check_amproc_signature(procform->amproc, INTERNALOID, false,
											3, 3, opckeytype, opckeytype,
											INTERNALOID);
				break;
			case MTREE_DISTANCE_PROC:
				ok = check_amproc_signature(procform->amproc, FLOAT8OID, false,
											5, 5, INTERNALOID, opcintype,
											INT2OID, OIDOID, INTERNALOID);
				break;
			default:
				ereport(INFO,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("operator family \"%s\" of access method %s contains function %s with invalid support number %d",
								opfamilyname, "mtree",
								format_procedure(procform->amproc),
								procform->amprocnum)));
				result = false;
				continue;		/* don't want additional message */
		}

		if (!ok)
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("operator family \"%s\" of access method %s contains function %s with wrong signature for support number %d",
							opfamilyname, "mtree",
							format_procedure(procform->amproc),
							procform->amprocnum)));
			result = false;
		}
	}

	/* Check individual operators */
	for (i = 0; i < oprlist->n_members; i++)
	{
		HeapTuple	oprtup = &oprlist->members[i]->tuple;
		Form_pg_amop oprform = (Form_pg_amop) GETSTRUCT(oprtup);
		Oid			op_rettype;

		if (oprform->amopstrategy < 1)
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("operator family \"%s\" of access method %s contains operator %s with invalid strategy number %d",
							opfamilyname, "mtree",
							format_operator(oprform->amopopr),
							oprform->amopstrategy)));
			result = false;
		}

		/* MTree supports ORDER BY operators */
		if (oprform->amoppurpose != AMOP_SEARCH)
		{
			/* ... but must have matching distance proc */
			if (!OidIsValid(get_opfamily_proc(opfamilyoid,
											  oprform->amoplefttype,
											  oprform->amoplefttype,
											  MTREE_DISTANCE_PROC)))
			{
				ereport(INFO,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("operator family \"%s\" of access method %s contains unsupported ORDER BY specification for operator %s",
								opfamilyname, "mtree",
								format_operator(oprform->amopopr))));
				result = false;
			}
			/* ... and operator result must match the claimed btree opfamily */
			op_rettype = get_op_rettype(oprform->amopopr);
			if (!opfamily_can_sort_type(oprform->amopsortfamily, op_rettype))
			{
				ereport(INFO,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("operator family \"%s\" of access method %s contains incorrect ORDER BY opfamily specification for operator %s",
								opfamilyname, "mtree",
								format_operator(oprform->amopopr))));
				result = false;
			}
		}
		else
		{
			/* Search operators must always return bool */
			op_rettype = BOOLOID;
		}

		/* Check operator signature */
		if (!check_amop_signature(oprform->amopopr, op_rettype,
								  oprform->amoplefttype,
								  oprform->amoprighttype))
		{
			ereport(INFO,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("operator family \"%s\" of access method %s contains operator %s with wrong signature",
							opfamilyname, "mtree",
							format_operator(oprform->amopopr))));
			result = false;
		}
	}

	/* Now check for inconsistent groups of operators/functions */
	grouplist = identify_opfamily_groups(oprlist, proclist);
	opclassgroup = NULL;
	foreach(lc, grouplist)
	{
		OpFamilyOpFuncGroup *thisgroup = (OpFamilyOpFuncGroup *) lfirst(lc);

		/* Remember the group exactly matching the test opclass */
		if (thisgroup->lefttype == opcintype &&
			thisgroup->righttype == opcintype)
			opclassgroup = thisgroup;

		/*
		 * There is not a lot we can do to check the operator sets, since each
		 * MTree opclass is more or less a law unto itself, and some contain
		 * only operators that are binary-compatible with the opclass datatype
		 * (meaning that empty operator sets can be OK).  That case also means
		 * that we shouldn't insist on nonempty function sets except for the
		 * opclass's own group.
		 */
	}

	/* Check that the originally-named opclass is complete */
	for (i = 1; i <= MTREENProcs; i++)
	{
		if (opclassgroup &&
			(opclassgroup->functionset & (((uint64) 1) << i)) != 0)
			continue;			/* got it */
		if (i == MTREE_DISTANCE_PROC)
			continue;			/* optional methods */
		ereport(INFO,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("operator class \"%s\" of access method %s is missing support function %d",
						opclassname, "mtree", i)));
		result = false;
	}

	ReleaseCatCacheList(proclist);
	ReleaseCatCacheList(oprlist);
	ReleaseSysCache(familytup);
	ReleaseSysCache(classtup);

	return result;
}
