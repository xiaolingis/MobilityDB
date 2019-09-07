/*****************************************************************************
 *
 * temporal_analyze.c
 *	  Functions for gathering statistics from temporal columns
 *
 * The function collects various kind of statistics for both the value and the
 * time dimension of temporal types. The kind of statistics depends on the 
 * duration of the temporal type, which is defined in the schema of table by 
 * the typmod attribute.
 * 
 * For TemporalInst
 * - Slot 0
 * 		- stakind contains the type of statistics which is STATISTIC_KIND_MCV.
 * 		- staop contains the OID of the "=" operator for the value dimension.
 * 		- stavalues stores the most common non-null values (MCV) for the value dimension.
 * 		- stanumbers stores the frequencies of the MCV for the value dimension.
 * 		- numnumbers contains the number of elements in the stanumbers array.
 * 		- numvalues contains the number of elements in the most common values array.
 * - Slot 1
 * 		- stakind contains the type of statistics which is STATISTIC_KIND_HISTOGRAM.
 * 		- staop contains the OID of the "<" operator that describes the sort ordering.
 * 		- stavalues stores the histogram of scalar data for the value dimension
 * 		- numvalues contains the number of buckets in the histogram.
 * - Slot 2
 * 		- stakind contains the type of statistics which is STATISTIC_KIND_MCV.
 * 		- staop contains the "=" operator of the time dimension.
 * 		- stavalues stores the most common values (MCV) for the time dimension.
 * 		- stanumbers stores the frequencies of the MCV for the time dimension.
 * 		- numnumbers contains the number of elements in the stanumbers array.
 * 		- numvalues contains the number of elements in the most common values array.
 * - Slot 3
 * 		- stakind contains the type of statistics which is STATISTIC_KIND_HISTOGRAM.
 * 		- staop contains the OID of the "<" operator that describes the sort ordering.
 * 		- stavalues stores the histogram for the time dimension.
 * For all other durations
 * - Slot 0
 * 		- stakind contains the type of statistics which is STATISTIC_KIND_BOUNDS_HISTOGRAM.
 * 		- staop contains the "=" operator of the value dimension.
 * 		- stavalues stores the histogram of ranges for the value dimension.
 * 		- numvalues contains the number of buckets in the histogram.
 * - Slot 1
 * 		- stakind contains the type of statistics which is STATISTIC_KIND_RANGE_LENGTH_HISTOGRAM.
 * 		- staop contains the "<" operator to the value dimension.
 * 		- stavalues stores the length of the histogram of ranges for the value dimension.
 * 		- numvalues contains the number of buckets in the histogram.
 * - Slot 2
 *      - stakind contains the type of statistics which is STATISTIC_KIND_PERIOD_BOUNDS_HISTOGRAM.
 * 		- staop contains the "=" operator of the time dimension.
 * 		- stavalues stores the histogram of periods for the time dimension.
 * 		- numvalues contains the number of buckets in the histogram.
 * - Slot 3
 * 		- stakind contains the type of statistics which is STATISTIC_KIND_PERIOD_LENGTH_HISTOGRAM.
 * 		- staop contains the "<" operator of the time dimension.
 * 		- stavalues stores the length of the histogram of periods for the time dimension.
 * 		- numvalues contains the number of buckets in the histogram.
 *
 * In the case of temporal types having a Period as bounding box, that is,
 * tbool and ttext, no statistics are collected for the value dimension and
 * the statistics for the temporal part are still stored in slots 2 and 3.
 * 
 * Portions Copyright (c) 2019, Esteban Zimanyi, Mahmoud Sakr, Mohamed Bakli,
 * 		Universite Libre de Bruxelles
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *****************************************************************************/

#include "temporal_analyze.h"

#include <assert.h>
#include <access/tuptoaster.h>
#include <catalog/pg_collation_d.h>
#include <catalog/pg_operator_d.h>
#include <commands/vacuum.h>
#include <parser/parse_oper.h>
#include <utils/datum.h>
#include <utils/fmgrprotos.h>
#include <utils/lsyscache.h>
#include <utils/timestamp.h>

#include "period.h"
#include "time_analyze.h"
#include "rangetypes_ext.h"
#include "temporaltypes.h"
#include "oidcache.h"
#include "temporal_util.h"
#include "temporal_analyze.h"

/*
 * To avoid consuming too much memory, IO and CPU load during analysis, and/or
 * too much space in the resulting pg_statistic rows, we ignore arrays that
 * are wider than TEMPORAL_WIDTH_THRESHOLD (after detoasting!).  Note that this
 * number is considerably more than the similar WIDTH_THRESHOLD limit used
 * in analyze.c's standard typanalyze code.
 */
#define TEMPORAL_WIDTH_THRESHOLD 0x10000

/*
 * While statistic functions are running, we keep a pointer to the extra data
 * here for use by assorted subroutines.  The functions doesn't
 * currently need to be re-entrant, so avoiding this is not worth the extra
 * notational cruft that would be needed.
 */
TemporalAnalyzeExtraData *temporal_extra_data;

/*****************************************************************************
 * Comparison functions for different data types
 * Functions copied from files analyze.c and rangetypes_typanalyze.c
 *****************************************************************************/

/*
 * qsort_arg comparator for sorting ScalarItems
 *
 * Aside from sorting the items, we update the tupnoLink[] array
 * whenever two ScalarItems are found to contain equal datums.  The array
 * is indexed by tupno; for each ScalarItem, it contains the highest
 * tupno that that item's datum has been found to be equal to.  This allows
 * us to avoid additional comparisons in compute_scalar_stats().
 */
static int
compare_scalars(const void *a, const void *b, void *arg)
{
	Datum da = ((const ScalarItem *) a)->value;
	int ta = ((const ScalarItem *) a)->tupno;
	Datum db = ((const ScalarItem *) b)->value;
	int tb = ((const ScalarItem *) b)->tupno;
	CompareScalarsContext *cxt = (CompareScalarsContext *) arg;
	int compare;

	compare = ApplySortComparator(da, false, db, false, cxt->ssup);
	if (compare != 0)
		return compare;

	/*
	 * The two datums are equal, so update cxt->tupnoLink[].
	 */
	if (cxt->tupnoLink[ta] < tb)
		cxt->tupnoLink[ta] = tb;
	if (cxt->tupnoLink[tb] < ta)
		cxt->tupnoLink[tb] = ta;

	/*
	 * For equal datums, sort by tupno
	 */
	return ta - tb;
}

/*
 * qsort comparator for sorting ScalarMCVItems by position
 */
static int
compare_mcvs(const void *a, const void *b)
{
	int da = ((const ScalarMCVItem *) a)->first;
	int db = ((const ScalarMCVItem *) b)->first;

	return da - db;
}

/*
 * Comparison function for sorting RangeBounds.
 */
static int
range_bound_qsort_cmp(const void *a1, const void *a2)
{
	RangeBound *r1 = (RangeBound *) a1;
	RangeBound *r2 = (RangeBound *) a2;
	return period_cmp_bounds(DatumGetTimestampTz(r1->val),
							 DatumGetTimestampTz(r2->val),
							 r1->lower, r2->lower,
							 r1->inclusive, r2->inclusive);
}

/*****************************************************************************
 * Generic statistics functions for non-spatial temporal types.
 * In these functions the last argument valuestats determines whether
 * statistics are computed for the value dimension, i.e., in the case of
 * temporal numbers. Otherwise, statistics are computed only for the temporal
 * dimension, i.e., in the the case of temporal booleans and temporal text.
 *****************************************************************************/

/* 
 * Compute statistics for scalar values, used both for the value and the 
 * time components of TemporalInst columns.
 * Function derived from compute_scalar_stats of file analyze.c 
 */
static void
scalar_compute_stats(VacAttrStats *stats, ScalarItem *values, int *tupnoLink,
					 ScalarMCVItem *track, int nonnull_cnt, int null_cnt, Oid valuetypid,
					 int slot_idx, int total_width, int totalrows, int samplerows)
{
	double corr_xysum;
	SortSupportData ssup;
	Oid ltopr, eqopr;
	int track_cnt = 0,
		num_bins = stats->attr->attstattarget,
		num_mcv = stats->attr->attstattarget,
		num_hist,
		typlen;
	bool typbyval;

	if (valuetypid == TIMESTAMPTZOID)
	{
		typbyval = true;
		typlen = sizeof(TimestampTz);
	}
	else 
	{
		typbyval = type_byval_fast(valuetypid);
		typlen = get_typlen_fast(valuetypid);
	}

	/* We need to change the OID due to PostgreSQL internal behavior */
	if (valuetypid == INT4OID)
		valuetypid = INT8OID;

	MemoryContext old_cxt;
	int 	ndistinct,	/* # distinct values in sample */
			nmultiple,	/* # that appear multiple times */
			dups_cnt,
			i;
	CompareScalarsContext cxt;

	memset(&ssup, 0, sizeof(ssup));
	ssup.ssup_cxt = CurrentMemoryContext;
	/* We always use the default collation for statistics */
	ssup.ssup_collation = DEFAULT_COLLATION_OID;
	ssup.ssup_nulls_first = false;

	/*
	 * For now, don't perform abbreviated key conversion, because full values
	 * are required for MCV slot generation. Supporting that optimization
	 * would necessitate teaching compare_scalars() to call a tie-breaker.
	 */
	ssup.abbreviate = false;

	get_sort_group_operators(valuetypid,
							 false, false, false,
							 &ltopr, &eqopr, NULL,
							 NULL);
	PrepareSortSupportFromOrderingOp(ltopr, &ssup);

	/* Sort the collected values */
	cxt.ssup = &ssup;
	cxt.tupnoLink = tupnoLink;
	qsort_arg((void *) values, (size_t)nonnull_cnt, sizeof(ScalarItem),
			  compare_scalars, (void *) &cxt);

	/* Must copy the target values into anl_context */
	old_cxt = MemoryContextSwitchTo(stats->anl_context);

	corr_xysum = 0;
	ndistinct = 0;
	nmultiple = 0;
	dups_cnt = 0;
	for (i = 0; i < nonnull_cnt; i++)
	{
		int tupno = values[i].tupno;
		corr_xysum += ((double) i) * ((double) tupno);
		dups_cnt++;
		if (tupnoLink[tupno] == tupno)
		{
			/* Reached end of duplicates of this value */
			ndistinct++;
			if (dups_cnt > 1)
			{
				nmultiple++;
				if (track_cnt < num_mcv ||
					dups_cnt > track[track_cnt - 1].count)
				{
					/*
					 * Found a new item for the mcv list; find its
					 * position, bubbling down old items if needed. Loop
					 * invariant is that j points at an empty/replaceable
					 * slot.
					 */
					int j;

					if (track_cnt < num_mcv)
						track_cnt++;
					for (j = track_cnt - 1; j > 0; j--)
					{
						if (dups_cnt <= track[j - 1].count)
							break;
						track[j].count = track[j - 1].count;
						track[j].first = track[j - 1].first;
					}
					track[j].count = dups_cnt;
					track[j].first = i + 1 - dups_cnt;
				}
			}
			dups_cnt = 0;
		}
	}

	/*
	* Decide how many values are worth storing as most-common values. If
	* we are able to generate a complete MCV list (all the values in the
	* sample will fit, and we think these are all the ones in the table),
	* then do so.  Otherwise, store only those values that are
	* significantly more common than the (estimated) average. We set the
	* threshold rather arbitrarily at 25% more than average, with at
	* least 2 instances in the sample.  Also, we won't suppress values
	* that have a frequency of at least 1/K where K is the intended
	* number of histogram bins; such values might otherwise cause us to
	* emit duplicate histogram bin boundaries.  (We might end up with
	* duplicate histogram entries anyway, if the distribution is skewed;
	* but we prefer to treat such values as MCVs if at all possible.)
	*
	* Note: the first of these cases is meant to address columns with
	* small, fixed sets of possible values, such as boolean or enum
	* columns.  If we can *completely* represent the column population by
	* an MCV list that will fit into the stats target, then we should do
	* so and thus provide the planner with complete information.  But if
	* the MCV list is not complete, it's generally worth being more
	* selective, and not just filling it all the way up to the stats
	* target.  So for an incomplete list, we try to take only MCVs that
	* are significantly more common than average.
	*/
	if (((track_cnt) == (ndistinct == 0)) &&
		(stats->stadistinct > 0) &&
		(track_cnt <= num_mcv))
	{
		/* Track list includes all values seen, and all will fit */
		num_mcv = track_cnt;
	}
	else
	{
		double ndistinct_table = stats->stadistinct;
		double avgcount,
				mincount,
				maxmincount;

		/* Re-extract estimate of # distinct nonnull values in table */
		if (ndistinct_table < 0)
			ndistinct_table = -ndistinct_table * totalrows;
		/* estimate # occurrences in sample of a typical nonnull value */
		avgcount = (double) nonnull_cnt / ndistinct_table;
		/* set minimum threshold count to store a value */
		mincount = avgcount * 1.25;
		if (mincount < 2)
			mincount = 2;
		/* don't let threshold exceed 1/K, however */
		maxmincount = (double) nonnull_cnt / (double) num_bins;
		if (mincount > maxmincount)
			mincount = maxmincount;
		if (num_mcv > track_cnt)
			num_mcv = track_cnt;
		for (i = 0; i < num_mcv; i++)
		{
			if (track[i].count < mincount)
			{
				num_mcv = i;
				break;
			}
		}
	}

	/* Generate MCV slot entry */
	if (num_mcv > 0)
	{
		MemoryContext old_context;
		Datum *mcv_values;
		float4 *mcv_freqs;

		/* Must copy the target values into anl_context */
		old_context = MemoryContextSwitchTo(stats->anl_context);
		mcv_values = (Datum *) palloc(num_mcv * sizeof(Datum));
		mcv_freqs = (float4 *) palloc(num_mcv * sizeof(float4));

		for (i = 0; i < num_mcv; i++)
		{
			mcv_values[i] = datum_copy(values[track[i].first].value, valuetypid);
			mcv_freqs[i] = (float4) track[i].count / (float4) samplerows;
		}
		MemoryContextSwitchTo(old_context);

		stats->stakind[slot_idx] = STATISTIC_KIND_MCV;
		stats->staop[slot_idx] = eqopr;
		stats->stanumbers[slot_idx] = mcv_freqs;
		stats->numnumbers[slot_idx] = num_mcv;
		stats->stavalues[slot_idx] = mcv_values;
		stats->numvalues[slot_idx] = num_mcv;
		stats->statyplen[slot_idx] = (int16) typlen;
		stats->statypid[slot_idx] = valuetypid;
		stats->statypbyval[slot_idx] = typbyval;
	}
	slot_idx++;

	/*
	* Generate a histogram slot entry if there are at least two distinct
	* values not accounted for in the MCV list.  (This ensures the
	* histogram won't collapse to empty or a singleton.)
	*/
	num_hist = ndistinct - num_mcv;
	if (num_hist > num_bins)
		num_hist = num_bins + 1;
	if (num_hist >= 2)
	{
		MemoryContext old_context;
		Datum *hist_values;
		int nvals,
				pos,
				posfrac,
				delta,
				deltafrac;

		/* Sort the MCV items into position order to speed next loop */
		qsort((void *) track, num_mcv, sizeof(ScalarMCVItem), compare_mcvs);

		/*
		 * Collapse out the MCV items from the values[] array.
		 *
		 * Note we destroy the values[] array here... but we don't need it
		 * for anything more.  We do, however, still need values_cnt.
		 * nvals will be the number of remaining entries in values[].
		*/
		if (num_mcv > 0)
		{
			int src,
					dest,
					j;

			src = dest = 0;
			j = 0;			/* index of next interesting MCV item */
			while (src < nonnull_cnt)
			{
				int ncopy;

				if (j < num_mcv)
				{
					int first = track[j].first;

					if (src >= first)
					{
						/* advance past this MCV item */
						src = first + track[j].count;
						j++;
						continue;
					}
					ncopy = first - src;
				}
				else
					ncopy = nonnull_cnt - src;
				memmove(&values[dest], &values[src],
						ncopy * sizeof(ScalarItem));
				src += ncopy;
				dest += ncopy;
			}
			nvals = dest;
		}
		else
			nvals = nonnull_cnt;
		Assert(nvals >= num_hist);

		/* Must copy the target values into anl_context */
		old_context = MemoryContextSwitchTo(stats->anl_context);
		hist_values = (Datum *) palloc(num_hist * sizeof(Datum));

		/*
		* The object of this loop is to copy the first and last values[]
		* entries along with evenly-spaced values in between.  So the
		* i'th value is values[(i * (nvals - 1)) / (num_hist - 1)].  But
		* computing that subscript directly risks integer overflow when
		* the stats target is more than a couple thousand.  Instead we
		* add (nvals - 1) / (num_hist - 1) to pos at each step, tracking
		* the integral and fractional parts of the sum separately.
		*/
		delta = (nvals - 1) / (num_hist - 1);
		deltafrac = (nvals - 1) % (num_hist - 1);
		pos = posfrac = 0;

		for (i = 0; i < num_hist; i++)
		{
			hist_values[i] =
					datum_copy(values[pos].value, valuetypid);
			pos += delta;
			posfrac += deltafrac;
			if (posfrac >= (num_hist - 1))
			{
				/* fractional part exceeds 1, carry to integer part */
				pos++;
				posfrac -= (num_hist - 1);
			}
		}

		MemoryContextSwitchTo(old_context);

		stats->stakind[slot_idx] = STATISTIC_KIND_HISTOGRAM;
		stats->staop[slot_idx] = ltopr;
		stats->stavalues[slot_idx] = hist_values;
		stats->numvalues[slot_idx] = num_hist;
		stats->statyplen[slot_idx] = (int16)typlen;
		stats->statypid[slot_idx] = valuetypid;
		stats->statypbyval[slot_idx] = true;
		slot_idx++;
	}

	MemoryContextSwitchTo(old_cxt);
}

/* 
 * Compute statistics for TemporalInst columns.
 * Function derived from compute_scalar_stats of file analyze.c 
 */
static void
tempinst_compute_stats(VacAttrStats *stats, AnalyzeAttrFetchFunc fetchfunc,
					   int samplerows, double totalrows, bool valuestats)
{
	int null_cnt = 0,
		nonnull_cnt = 0,
		slot_idx = 0;
	double total_width = 0;
	ScalarItem *scalar_values, *timestamp_values;
	int *scalar_tupnoLink, *timestamp_tupnoLink;
	ScalarMCVItem *scalar_track, *timestamp_track;
	int num_mcv = stats->attr->attstattarget;
	Oid valuetypid;
	bool typbyval;

	if (valuestats)
	{
		scalar_values = (ScalarItem *) palloc(samplerows * sizeof(ScalarItem));
		scalar_tupnoLink = (int *) palloc(samplerows * sizeof(int));
		scalar_track = (ScalarMCVItem *) palloc(num_mcv * sizeof(ScalarMCVItem));
		valuetypid = base_oid_from_temporal(stats->attrtypid);
		typbyval = type_byval_fast(valuetypid);
	}

	timestamp_values = (ScalarItem *) palloc(samplerows * sizeof(ScalarItem));
	timestamp_tupnoLink = (int *) palloc(samplerows * sizeof(int));
	timestamp_track = (ScalarMCVItem *) palloc(num_mcv * sizeof(ScalarMCVItem));

	/* Loop over the sample values. */
	for (int i = 0; i < samplerows; i++)
	{
		Datum value;
		bool isnull;
		TemporalInst *inst;

		/* missing comment */
		vacuum_delay_point();

		value = fetchfunc(stats, i, &isnull);
		if (isnull)
		{
			/* TemporalInst is null, just count that */
			null_cnt++;
			continue;
		}

		total_width += VARSIZE_ANY(DatumGetPointer(value));

		/* Get TemporalInst value */
		inst = DatumGetTemporalInst(value);

		if (valuestats)
		{
			if (typbyval)
				scalar_values[nonnull_cnt].value = datum_copy(temporalinst_value(inst), valuetypid);
			else
				scalar_values[nonnull_cnt].value = PointerGetDatum(temporalinst_value(inst));
			scalar_values[nonnull_cnt].tupno = i;
			scalar_tupnoLink[nonnull_cnt] = i;
		}
		timestamp_values[nonnull_cnt].value = datum_copy(TimestampGetDatum(inst->t), TIMESTAMPTZOID);
		timestamp_values[nonnull_cnt].tupno = i;
		timestamp_tupnoLink[nonnull_cnt] = i;

		nonnull_cnt++;
	}

	/* We can only compute real stats if we found some non-null values. */
	if (nonnull_cnt > 0)
	{
		stats->stats_valid = true;

		/* Do the simple null-frac and width stats */
		stats->stanullfrac = (float4)((double) null_cnt / (double) samplerows);
		stats->stawidth = (int32) (total_width / (double) nonnull_cnt);

		/* Estimate that non-null values are unique */
		stats->stadistinct = -1.0f * (1.0f - stats->stanullfrac);

		if (valuestats)
		{
			/* Compute the statistics for the value dimension */
			scalar_compute_stats(stats, scalar_values, scalar_tupnoLink,
								scalar_track, nonnull_cnt, null_cnt, valuetypid,
								slot_idx, total_width, totalrows, samplerows);
		}

		slot_idx += 2;

		/* Compute the statistics for the time dimension */
		scalar_compute_stats(stats, timestamp_values, timestamp_tupnoLink,
							 timestamp_track, nonnull_cnt, null_cnt, TIMESTAMPTZOID,
							 slot_idx, total_width, totalrows, samplerows);
	}
	else if (null_cnt > 0)
	{
		/* We found only nulls; assume the column is entirely null */
		stats->stats_valid = true;
		stats->stanullfrac = 1.0;
		stats->stawidth = 0;			/* "unknown" */
		stats->stadistinct = 0.0;		/* "unknown" */
	}
}

/* 
 * Compute statistics for TemporalSeq and TemporalS columns.
 * Function derived from compute_range_stats of file rangetypes_typanalyze.c 
 */
static void
temps_compute_stats(VacAttrStats *stats, AnalyzeAttrFetchFunc fetchfunc,
					int samplerows, double totalrows, bool valuestats)
{
	int null_cnt = 0,
		analyzed_rows = 0,
		slot_idx,
		num_bins = stats->attr->attstattarget,
		num_hist;
	float8 *value_lengths, 
		   *time_lengths;
	RangeBound *value_lowers,
			*value_uppers;
	PeriodBound *time_lowers,
			*time_uppers;
	double total_width = 0;
	Oid rangetypid;

	temporal_extra_data = (TemporalAnalyzeExtraData *)stats->extra_data;

	if (valuestats)
	{
		/* This function is valid for temporal numbers */
		numeric_base_type_oid(temporal_extra_data->value_type_id);
		if (temporal_extra_data->value_type_id == INT4OID)
			rangetypid = type_oid(T_INTRANGE);
		else if (temporal_extra_data->value_type_id == FLOAT8OID)
			rangetypid = type_oid(T_FLOATRANGE);
		value_lowers = (RangeBound *) palloc(sizeof(RangeBound) * samplerows);
		value_uppers = (RangeBound *) palloc(sizeof(RangeBound) * samplerows);
		value_lengths = (float8 *) palloc(sizeof(float8) * samplerows);
	}
	time_lowers = (PeriodBound *) palloc(sizeof(PeriodBound) * samplerows);
	time_uppers = (PeriodBound *) palloc(sizeof(PeriodBound) * samplerows);
	time_lengths = (float8 *) palloc(sizeof(float8) * samplerows);

	/* Loop over the temporal values. */
	for (int i = 0; i < samplerows; i++)
	{
		Datum value;
		bool isnull, isempty;
		RangeType *range;
		TypeCacheEntry *typcache;
		RangeBound range_lower,
				range_upper;
		Period period;
		PeriodBound period_lower,
				period_upper;
		float8 value_length = 0, time_length = 0;
		Temporal *temp;
	
		vacuum_delay_point();

		value = fetchfunc(stats, i, &isnull);
		if (isnull)
		{
			/* Temporal is null, just count that */
			null_cnt++;
			continue;
		}

		/* Skip too-large values. */
		if (toast_raw_datum_size(value) > TEMPORAL_WIDTH_THRESHOLD)
			continue;

		total_width += VARSIZE_ANY(DatumGetPointer(value));

		/* Get Temporal value */
		temp = DatumGetTemporal(value);

		/* Remember bounds and length for further usage in histograms */
		if (valuestats)
		{
			range = tnumber_value_range_internal(temp);
			typcache = lookup_type_cache(rangetypid, TYPECACHE_RANGE_INFO);
			range_deserialize(typcache, range, &range_lower, &range_upper, &isempty);
			value_lowers[analyzed_rows] = range_lower;
			value_uppers[analyzed_rows] = range_upper;

			if (temporal_extra_data->value_type_id == INT4OID)
				value_length = DatumGetFloat8(value_uppers[analyzed_rows].val) -
							DatumGetFloat8(value_lowers[analyzed_rows].val);
			else if (temporal_extra_data->value_type_id == FLOAT8OID)
				value_length = (float8) (DatumGetInt32(value_uppers[analyzed_rows].val) -
										DatumGetInt32(value_lowers[analyzed_rows].val));
			value_lengths[analyzed_rows] = value_length;
		}
		temporal_timespan_internal(&period, temp);
		period_deserialize(&period, &period_lower, &period_upper);
		time_lowers[analyzed_rows] = period_lower;
		time_uppers[analyzed_rows] = period_upper;
		time_length = period_duration_secs(period_upper.val, period_lower.val);
		time_lengths[analyzed_rows] = time_length;

		analyzed_rows++;
	}

	slot_idx = 0;

	/* We can only compute real stats if we found some non-null values. */
	if (analyzed_rows > 0)
	{
		int pos,
			posfrac,
			delta,
			deltafrac,
			i;
		MemoryContext old_cxt;

		stats->stats_valid = true;

		/* Do the simple null-frac and width stats */
		stats->stanullfrac = (float4) null_cnt / (float4) samplerows;
		stats->stawidth = (int) (total_width / analyzed_rows);

		/* Estimate that non-null values are unique */
		stats->stadistinct = (float4) (-1.0 * (1.0 - stats->stanullfrac));

		/* Must copy the target values into anl_context */
		old_cxt = MemoryContextSwitchTo(stats->anl_context);

		if (valuestats)
		{
			Datum *value_bound_hist_values;
			Datum *value_length_hist_values;

			/*
			* Generate value histograms if there are at least two values.
			*/
			if (analyzed_rows >= 2)
			{
				/* Generate a bounds histogram slot entry */

				/* Sort bound values */
				qsort(value_lowers, analyzed_rows, sizeof(RangeBound), range_bound_qsort_cmp);
				qsort(value_uppers, analyzed_rows, sizeof(RangeBound), range_bound_qsort_cmp);

				num_hist = analyzed_rows;
				if (num_hist > num_bins)
					num_hist = num_bins + 1;

				value_bound_hist_values = (Datum *) palloc(num_hist * sizeof(Datum));

				/*
				* The object of this loop is to construct ranges from first and
				* last entries in lowers[] and uppers[] along with evenly-spaced
				* values in between. So the i'th value is a range of lowers[(i *
				* (nvals - 1)) / (num_hist - 1)] and uppers[(i * (nvals - 1)) /
				* (num_hist - 1)]. But computing that subscript directly risks
				* integer overflow when the stats target is more than a couple
				* thousand.  Instead we add (nvals - 1) / (num_hist - 1) to pos
				* at each step, tracking the integral and fractional parts of the
				* sum separately.
				*/
				delta = (analyzed_rows - 1) / (num_hist - 1);
				deltafrac = (analyzed_rows - 1) % (num_hist - 1);
				pos = posfrac = 0;

				for (i = 0; i < num_hist; i++)
				{
					value_bound_hist_values[i] = PointerGetDatum(
							range_make(value_lowers[pos].val, value_uppers[pos].val,
									true, true, temporal_extra_data->value_type_id));

					pos += delta;
					posfrac += deltafrac;
					if (posfrac >= (num_hist - 1))
					{
						/* fractional part exceeds 1, carry to integer part */
						pos++;
						posfrac -= (num_hist - 1);
					}
				}

				TypeCacheEntry *range_typeentry = lookup_type_cache(rangetypid,
																	TYPECACHE_EQ_OPR |
																	TYPECACHE_CMP_PROC_FINFO |
																	TYPECACHE_HASH_PROC_FINFO);

				stats->stakind[slot_idx] = STATISTIC_KIND_BOUNDS_HISTOGRAM;
				stats->staop[slot_idx] = temporal_extra_data->value_eq_opr;
				stats->stavalues[slot_idx] = value_bound_hist_values;
				stats->numvalues[slot_idx] = num_hist;
				stats->statypid[slot_idx] = range_typeentry->type_id;
				stats->statyplen[slot_idx] = range_typeentry->typlen;
				stats->statypbyval[slot_idx] =range_typeentry->typbyval;
				stats->statypalign[slot_idx] = range_typeentry->typalign;

				slot_idx++;

				/* Generate a length histogram slot entry */

				/*
				* Ascending sort of range lengths for further filling of
				* histogram
				*/
				qsort(value_lengths, analyzed_rows, sizeof(float8), float8_qsort_cmp);

				num_hist = analyzed_rows;
				if (num_hist > num_bins)
					num_hist = num_bins + 1;

				value_length_hist_values = (Datum *) palloc(num_hist * sizeof(Datum));

				/*
				* The object of this loop is to copy the first and last lengths[]
				* entries along with evenly-spaced values in between. So the i'th
				* value is lengths[(i * (nvals - 1)) / (num_hist - 1)]. But
				* computing that subscript directly risks integer overflow when
				* the stats target is more than a couple thousand.  Instead we
				* add (nvals - 1) / (num_hist - 1) to pos at each step, tracking
				* the integral and fractional parts of the sum separately.
				*/
				delta = (analyzed_rows - 1) / (num_hist - 1);
				deltafrac = (analyzed_rows - 1) % (num_hist - 1);
				pos = posfrac = 0;

				for (i = 0; i < num_hist; i++)
				{
					value_length_hist_values[i] = Float8GetDatum(value_lengths[pos]);
					pos += delta;
					posfrac += deltafrac;
					if (posfrac >= (num_hist - 1))
					{
						/* fractional part exceeds 1, carry to integer part */
						pos++;
						posfrac -= (num_hist - 1);
					}
				}
			}
			else
			{
				/*
				* Even when we don't create the histogram, store an empty array
				* to mean "no histogram". We can't just leave stavalues NULL,
				* because get_attstatsslot() errors if you ask for stavalues, and
				* it's NULL. We'll still store the empty fraction in stanumbers.
				*/
				value_length_hist_values = palloc(0);
				num_hist = 0;
			}
			stats->stakind[slot_idx] = STATISTIC_KIND_RANGE_LENGTH_HISTOGRAM;
			stats->staop[slot_idx] = Float8LessOperator;
			stats->stavalues[slot_idx] = value_length_hist_values;
			stats->numvalues[slot_idx] = num_hist;
			stats->statypid[slot_idx] = FLOAT8OID;
			stats->statyplen[slot_idx] = sizeof(float8);
			stats->statypbyval[slot_idx] = true;
			stats->statypalign[slot_idx] = 'd';
		}

		slot_idx = 2;

		Datum *bound_hist_time;
		Datum *length_hist_time;

		/*
		 * Generate temporal histograms if there are at least two values.
		 */
		if (analyzed_rows >= 2)
		{
			/* Generate a bounds histogram slot entry */

			/* Sort bound values */
			qsort(time_lowers, analyzed_rows, sizeof(PeriodBound), period_bound_qsort_cmp);
			qsort(time_uppers, analyzed_rows, sizeof(PeriodBound), period_bound_qsort_cmp);

			num_hist = analyzed_rows;
			if (num_hist > num_bins)
				num_hist = num_bins + 1;

			bound_hist_time = (Datum *) palloc(num_hist * sizeof(Datum));

			/*
			 * The object of this loop is to construct periods from first and
			 * last entries in lowers[] and uppers[] along with evenly-spaced
			 * values in between. So the i'th value is a period of lowers[(i *
			 * (nvals - 1)) / (num_hist - 1)] and uppers[(i * (nvals - 1)) /
			 * (num_hist - 1)]. But computing that subscript directly risks
			 * integer overflow when the stats target is more than a couple
			 * thousand.  Instead we add (nvals - 1) / (num_hist - 1) to pos
			 * at each step, tracking the integral and fractional parts of the
			 * sum separately.
			 */
			delta = (analyzed_rows - 1) / (num_hist - 1);
			deltafrac = (analyzed_rows - 1) % (num_hist - 1);
			pos = posfrac = 0;

			for (i = 0; i < num_hist; i++)
			{
				bound_hist_time[i] =
						PointerGetDatum(period_make(time_lowers[pos].val, time_uppers[pos].val,
													time_lowers[pos].inclusive, time_uppers[pos].inclusive));

				pos += delta;
				posfrac += deltafrac;
				if (posfrac >= (num_hist - 1))
				{
					/* fractional part exceeds 1, carry to integer part */
					pos++;
					posfrac -= (num_hist - 1);
				}
			}

			stats->stakind[slot_idx] = STATISTIC_KIND_BOUNDS_HISTOGRAM;
			stats->staop[slot_idx] = temporal_extra_data->time_eq_opr; 
			stats->stavalues[slot_idx] = bound_hist_time;
			stats->numvalues[slot_idx] = num_hist;
			stats->statypid[slot_idx] = temporal_extra_data->time_type_id;
			stats->statyplen[slot_idx] = temporal_extra_data->time_typlen;
			stats->statypbyval[slot_idx] = temporal_extra_data->time_typbyval;
			stats->statypalign[slot_idx] = temporal_extra_data->time_typalign;
			slot_idx++;

			/* Generate a length histogram slot entry. */

			/*
			 * Ascending sort of period lengths for further filling of
			 * histogram
			 */
			qsort(time_lengths, analyzed_rows, sizeof(float8), float8_qsort_cmp);

			num_hist = analyzed_rows;
			if (num_hist > num_bins)
				num_hist = num_bins + 1;

			length_hist_time = (Datum *) palloc(num_hist * sizeof(Datum));

			/*
			 * The object of this loop is to copy the first and last lengths[]
			 * entries along with evenly-spaced values in between. So the i'th
			 * value is lengths[(i * (nvals - 1)) / (num_hist - 1)]. But
			 * computing that subscript directly risks integer overflow when
			 * the stats target is more than a couple thousand.  Instead wes
			 * add (nvals - 1) / (num_hist - 1) to pos at each step, tracking
			 * the integral and fractional parts of the sum separately.
			 */
			delta = (analyzed_rows - 1) / (num_hist - 1);
			deltafrac = (analyzed_rows - 1) % (num_hist - 1);
			pos = posfrac = 0;

			for (i = 0; i < num_hist; i++)
			{
				length_hist_time[i] = Float8GetDatum(time_lengths[pos]);
				pos += delta;
				posfrac += deltafrac;
				if (posfrac >= (num_hist - 1))
				{
					/* fractional part exceeds 1, carry to integer part */
					pos++;
					posfrac -= (num_hist - 1);
				}
			}
		}
		else
		{
			/*
			 * Even when we don't create the histogram, store an empty array
			 * to mean "no histogram". We can't just leave stavalues NULL,
			 * because get_attstatsslot() errors if you ask for stavalues, and
			 * it's NULL.
			 */
			length_hist_time = palloc(0);
			num_hist = 0;
		}
		stats->stakind[slot_idx] = STATISTIC_KIND_PERIOD_LENGTH_HISTOGRAM;
		stats->staop[slot_idx] = Float8LessOperator;
		stats->stavalues[slot_idx] = length_hist_time;
		stats->numvalues[slot_idx] = num_hist;
		stats->statypid[slot_idx] = FLOAT8OID;
		stats->statyplen[slot_idx] = sizeof(float8);
		stats->statypbyval[slot_idx] = true;
		stats->statypalign[slot_idx] = 'd';

		MemoryContextSwitchTo(old_cxt);
	}
	else if (null_cnt > 0)
	{
		/* We found only nulls; assume the column is entirely null */
		stats->stats_valid = true;
		stats->stanullfrac = 1.0;
		stats->stawidth = 0;		/* "unknown" */
		stats->stadistinct = 0.0;	/* "unknown" */
	}

	/*
	 * We don't need to bother cleaning up any of our temporary palloc's. The
	 * hashtable should also go away, as it used a child memory context.
	 */
}

/*****************************************************************************
 * Statistics functions for temporal types
 *****************************************************************************/

void
temporalinst_compute_stats(VacAttrStats *stats, AnalyzeAttrFetchFunc fetchfunc,
						   int samplerows, double totalrows)
{
	return tempinst_compute_stats(stats, fetchfunc, samplerows, totalrows, false);
}

void
temporals_compute_stats(VacAttrStats *stats, AnalyzeAttrFetchFunc fetchfunc,
						int samplerows, double totalrows)
{
	return temps_compute_stats(stats, fetchfunc, samplerows, totalrows, false);
}

/*****************************************************************************
 * Statistics functions for temporal types
 *****************************************************************************/

void
tnumberinst_compute_stats(VacAttrStats *stats, AnalyzeAttrFetchFunc fetchfunc,
						   int samplerows, double totalrows)
{
	return tempinst_compute_stats(stats, fetchfunc, samplerows, totalrows, true);
}

void
tnumbers_compute_stats(VacAttrStats *stats, AnalyzeAttrFetchFunc fetchfunc,
						int samplerows, double totalrows)
{
	return temps_compute_stats(stats, fetchfunc, samplerows, totalrows, true);
}

/*****************************************************************************
 * Statistics information for temporal types
 *****************************************************************************/

void
temporal_extra_info(VacAttrStats *stats)
{
	TypeCacheEntry *typentry;
	TemporalAnalyzeExtraData *extra_data;
	Form_pg_attribute attr = stats->attr;

	/*
	 * Check attribute data type is a temporal type.
	 */
	if (! temporal_type_oid(stats->attrtypid))
		elog(ERROR, "temporal_analyze was invoked with invalid type %u",
			 stats->attrtypid);

	/* Store our findings for use by stats functions */
	extra_data = (TemporalAnalyzeExtraData *) palloc(sizeof(TemporalAnalyzeExtraData));

	/*
	 * Gather information about the temporal type and its value and time types.
	 */

	/* Information about the temporal type */
	typentry = lookup_type_cache(stats->attrtypid,
								 TYPECACHE_EQ_OPR | TYPECACHE_LT_OPR |
								 TYPECACHE_CMP_PROC_FINFO |
								 TYPECACHE_HASH_PROC_FINFO);
	extra_data->type_id = typentry->type_id;
	extra_data->eq_opr = typentry->eq_opr;
	extra_data->lt_opr = typentry->lt_opr;
	extra_data->typbyval = typentry->typbyval;
	extra_data->typlen = typentry->typlen;
	extra_data->typalign = typentry->typalign;
	extra_data->cmp = &typentry->cmp_proc_finfo;
	extra_data->hash = &typentry->hash_proc_finfo;

	/* Information about the value type */
	typentry = lookup_type_cache(base_oid_from_temporal(stats->attrtypid),
										TYPECACHE_EQ_OPR | TYPECACHE_LT_OPR |
										TYPECACHE_CMP_PROC_FINFO |
										TYPECACHE_HASH_PROC_FINFO);
	extra_data->value_type_id = typentry->type_id;
	extra_data->value_eq_opr = typentry->eq_opr;
	extra_data->value_lt_opr = typentry->lt_opr;
	extra_data->value_typbyval = typentry->typbyval;
	extra_data->value_typlen = typentry->typlen;
	extra_data->value_typalign = typentry->typalign;
	extra_data->value_cmp = &typentry->cmp_proc_finfo;
	extra_data->value_hash = &typentry->hash_proc_finfo;

	/* Information about the time type */
    if (stats->attrtypmod == TEMPORALINST)
    {
        typentry = lookup_type_cache(TIMESTAMPTZOID,
										  TYPECACHE_EQ_OPR | TYPECACHE_LT_OPR |
                                          TYPECACHE_CMP_PROC_FINFO |
                                          TYPECACHE_HASH_PROC_FINFO);
        extra_data->time_type_id = TIMESTAMPTZOID;
        extra_data->time_eq_opr = typentry->eq_opr;
        extra_data->time_lt_opr = typentry->lt_opr;
        extra_data->time_typbyval = false;
        extra_data->time_typlen = sizeof(TimestampTz);
        extra_data->time_typalign = 'd';
        extra_data->time_cmp = &typentry->cmp_proc_finfo;
        extra_data->time_hash = &typentry->hash_proc_finfo;
    }
    else
    {
		Oid pertypoid = type_oid(T_PERIOD);
        typentry = lookup_type_cache(pertypoid,
										  TYPECACHE_EQ_OPR | TYPECACHE_LT_OPR |
                                          TYPECACHE_CMP_PROC_FINFO |
                                          TYPECACHE_HASH_PROC_FINFO);
        extra_data->time_type_id = pertypoid;
        extra_data->time_eq_opr = typentry->eq_opr;
        extra_data->time_lt_opr = typentry->lt_opr;
        extra_data->time_typbyval = false;
        extra_data->time_typlen = sizeof(Period);
        extra_data->time_typalign = 'd';
        extra_data->time_cmp = &typentry->cmp_proc_finfo;
        extra_data->time_hash = &typentry->hash_proc_finfo;
    }

	extra_data->std_extra_data = stats->extra_data;
	stats->extra_data = extra_data;

	stats->minrows = 300 * attr->attstattarget;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(temporal_analyze);

PGDLLEXPORT Datum
temporal_analyze(PG_FUNCTION_ARGS)
{
	VacAttrStats *stats = (VacAttrStats *) PG_GETARG_POINTER(0);
	int duration;

	/*
	 * Call the standard typanalyze function.  It may fail to find needed
	 * operators, in which case we also can't do anything, so just fail.
	 */
	if (!std_typanalyze(stats))
		PG_RETURN_BOOL(false);

	/* 
	 * Collect extra information about the temporal type and its value
	 * and time types
	 */
	temporal_extra_info(stats);

	/* Ensure duration is valid */
	duration = TYPMOD_GET_DURATION(stats->attrtypmod);
	temporal_duration_all_is_valid(duration);
	if (duration == TEMPORALINST)
		stats->compute_stats = temporalinst_compute_stats;
	else
    	stats->compute_stats = temporals_compute_stats;

	PG_RETURN_BOOL(true);
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(tnumber_analyze);

PGDLLEXPORT Datum
tnumber_analyze(PG_FUNCTION_ARGS)
{
	VacAttrStats *stats = (VacAttrStats *) PG_GETARG_POINTER(0);
	int duration;

	/*
	 * Call the standard typanalyze function.  It may fail to find needed
	 * operators, in which case we also can't do anything, so just fail.
	 */
	if (!std_typanalyze(stats))
		PG_RETURN_BOOL(false);

	/* 
	 * Collect extra information about the temporal type and its value
	 * and time types
	 */
	temporal_extra_info(stats);

	/*
	 * Ensure duration is valid and call the corresponding function to 
	 * compute statistics.
	 */
	duration = TYPMOD_GET_DURATION(stats->attrtypmod);
	temporal_duration_all_is_valid(duration);
	if (duration == TEMPORALINST)
		stats->compute_stats = tnumberinst_compute_stats;
	else
		stats->compute_stats = tnumbers_compute_stats;

	PG_RETURN_BOOL(true);
}

/*****************************************************************************/
