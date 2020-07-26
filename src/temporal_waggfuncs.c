/*****************************************************************************
 *
 * temporal_waggfuncs.c
 *	  Window temporal aggregate functions
 *
 * Portions Copyright (c) 2020, Esteban Zimanyi, Arthur Lesuisse,
 *		Universite Libre de Bruxelles
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *****************************************************************************/

#include "temporal_waggfuncs.h"

#include <utils/builtins.h>
#include <utils/timestamp.h>

#include "temporaltypes.h"
#include "oidcache.h"
#include "temporal_util.h"
#include "temporal_aggfuncs.h"
#include "doublen.h"

/*****************************************************************************
 * Generic functions
 *****************************************************************************/

/**
 * Extend the temporal instant value by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] inst Temporal value
 * @param[in] interval Interval
 */
static int
temporalinst_extend(TemporalSeq **result, const TemporalInst *inst,
	const Interval *interval)
{
	/* Should be additional attribute */
	bool linear = linear_interpolation(inst->valuetypid);
	TemporalInst *instants[2];
	TimestampTz upper = DatumGetTimestampTz(
		DirectFunctionCall2(timestamptz_pl_interval,
		TimestampTzGetDatum(inst->t),
		PointerGetDatum(interval)));
	instants[0] = (TemporalInst *) inst;
	instants[1] = temporalinst_make(temporalinst_value(inst), upper,
		inst->valuetypid);
	result[0] = temporalseq_make(instants, 2, true, true,
		linear, false);
	pfree(instants[1]);
	return 1;
}

/**
 * Extend the temporal instant set value by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] ti Temporal value
 * @param[in] interval Interval
 */
static int
temporali_extend(TemporalSeq **result, const TemporalI *ti,
	const Interval *interval)
{
	for (int i = 0; i < ti->count; i++)
	{
		TemporalInst *inst = temporali_inst_n(ti, i);
		temporalinst_extend(&result[i], inst, interval);
	}
	return ti->count;
}

/**
 * Extend the temporal sequence value with stepwise interpolation by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] seq Temporal value
 * @param[in] interval Interval
 */
static int
tstepseq_extend(TemporalSeq **result, const TemporalSeq *seq, 
	const Interval *interval)
{
	if (seq->count == 1)
		return temporalinst_extend(result, temporalseq_inst_n(seq, 0), interval);
	
	TemporalInst *instants[2];
	TemporalInst *inst1 = temporalseq_inst_n(seq, 0);
	bool linear = MOBDB_FLAGS_GET_LINEAR(seq->flags);
	bool lower_inc = seq->period.lower_inc;
	for (int i = 0; i < seq->count - 1; i++)
	{
		TemporalInst *inst2 = temporalseq_inst_n(seq, i + 1);
		bool upper_inc = (i == seq->count - 2) ? seq->period.upper_inc : false ;
		TimestampTz upper = DatumGetTimestampTz(
			DirectFunctionCall2(timestamptz_pl_interval,
			TimestampTzGetDatum(inst2->t),
			PointerGetDatum(interval)));
		instants[0] = inst1;
		instants[1] = temporalinst_make(temporalinst_value(inst1), 
			upper, inst1->valuetypid);
		result[i] = temporalseq_make(instants, 2, lower_inc, upper_inc, linear, false);
		pfree(instants[1]);
		inst1 = inst2;
		lower_inc = true;
	}
	return seq->count - 1;
}

/**
 * Extend the temporal sequence value with linear interpolation by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] seq Temporal value
 * @param[in] interval Interval
 * @param[in] min True if the calling function is min (max otherwise)
 */
static int
tlinearseq_extend(TemporalSeq **result, const TemporalSeq *seq,
	const Interval *interval, bool min)
{
	if (seq->count == 1)
		return temporalinst_extend(result, temporalseq_inst_n(seq, 0), interval);

	TemporalInst *instants[3];
	TemporalInst *inst1 = temporalseq_inst_n(seq, 0);
	Datum value1 = temporalinst_value(inst1);
	bool linear = MOBDB_FLAGS_GET_LINEAR(seq->flags);
	bool lower_inc = seq->period.lower_inc;
	for (int i = 0; i < seq->count - 1; i++)
	{
		TemporalInst *inst2 = temporalseq_inst_n(seq, i + 1);
		Datum value2 = temporalinst_value(inst2);
		bool upper_inc = (i == seq->count - 2) ? seq->period.upper_inc : false ;

		/* Constant segment */
		if (datum_eq(value1, value2, inst1->valuetypid))
		{
			TimestampTz upper = DatumGetTimestampTz(DirectFunctionCall2(
				timestamptz_pl_interval, TimestampTzGetDatum(inst2->t),
				PointerGetDatum(interval)));
			instants[0] = inst1;
			instants[1] = temporalinst_make(value1, upper, inst1->valuetypid);
			result[i] = temporalseq_make(instants, 2, lower_inc, upper_inc, linear, false);
			pfree(instants[1]);
		}
		else
		{
			/* Increasing period and minimum function or
			 * decreasing period and maximum function */
			if ((datum_lt(value1, value2, inst1->valuetypid) && min) ||
				(datum_gt(value1, value2, inst1->valuetypid) && !min))
			{
				/* Extend the start value for the duration of the window */
				TimestampTz lower = DatumGetTimestampTz(DirectFunctionCall2(
					timestamptz_pl_interval, TimestampTzGetDatum(inst1->t),
					PointerGetDatum(interval)));
				TimestampTz upper = DatumGetTimestampTz(DirectFunctionCall2(
					timestamptz_pl_interval, TimestampTzGetDatum(inst2->t),
					PointerGetDatum(interval)));
				instants[0] = inst1;
				instants[1] = temporalinst_make(value1, lower, inst1->valuetypid);
				instants[2] = temporalinst_make(value2, upper, inst1->valuetypid);
				result[i] = temporalseq_make(instants, 3, lower_inc, upper_inc, linear, false);
				pfree(instants[1]); pfree(instants[2]);
			}
			else
			{
				/* Extend the end value for the duration of the window */
				TimestampTz upper = DatumGetTimestampTz(DirectFunctionCall2(
					timestamptz_pl_interval, TimestampTzGetDatum(seq->period.upper),
					PointerGetDatum(interval)));
				instants[0] = inst1;
				instants[1] = inst2;
				instants[2] = temporalinst_make(value2, upper, inst1->valuetypid);
				result[i] = temporalseq_make(instants, 3, lower_inc, upper_inc, linear, false);
				pfree(instants[2]);
			}
		}
		inst1 = inst2;
		lower_inc = true;
	}	
	return seq->count - 1;
}

/**
 * Extend the temporal sequence set value with stepwise interpolation by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] ts Temporal value
 * @param[in] interval Interval
 */
static int
tsteps_extend(TemporalSeq **result, const TemporalS *ts,
	const Interval *interval)
{
	if (ts->count == 1)
		return tstepseq_extend(result, temporals_seq_n(ts, 0), interval);

	int k = 0;
	for (int i = 0; i < ts->count; i++)
	{
		TemporalSeq *seq = temporals_seq_n(ts, i);
		k += tstepseq_extend(&result[k], seq, interval);
	}
	return k;
}

/**
 * Extend the temporal sequence set value with linear interpolation by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] ts Temporal value
 * @param[in] interval Interval
 * @param[in] min True if the calling function is min (max otherwise)
 */
static int
tlinears_extend(TemporalSeq **result, const TemporalS *ts,
	const Interval *interval, bool min)
{
	if (ts->count == 1)
		return tstepseq_extend(result, temporals_seq_n(ts, 0), interval);

	int k = 0;
	for (int i = 0; i < ts->count; i++)
	{
		TemporalSeq *seq = temporals_seq_n(ts, i);
		k += tlinearseq_extend(&result[k], seq, interval, min);
	}
	return k;
}

/**
 * Extend the temporal value by the time interval (dispatch function)
 *
 * @param[in] temp Temporal value
 * @param[in] interval Interval
 * @param[in] min True if the calling function is min (max otherwise)
 * @param[out] count Number of elements in the output array
 */
static TemporalSeq **
temporal_extend(Temporal *temp, Interval *interval, bool min, int *count)
{
	TemporalSeq **result;
	ensure_valid_duration(temp->duration);
	if (temp->duration == TEMPORALINST)
	{
		TemporalInst *inst = (TemporalInst *)temp;
		result = palloc(sizeof(TemporalSeq *));
		*count = temporalinst_extend(result, inst, interval);
	}
	else if (temp->duration == TEMPORALI)
	{
		TemporalI *ti = (TemporalI *)temp;
		result = palloc(sizeof(TemporalSeq *) * ti->count);
		*count = temporali_extend(result, ti, interval);
	}
	else if (temp->duration == TEMPORALSEQ)
	{
		TemporalSeq *seq = (TemporalSeq *)temp;
		result = palloc(sizeof(TemporalSeq *) * seq->count);
		if (! MOBDB_FLAGS_GET_LINEAR(temp->flags))
			*count = tstepseq_extend(result, seq, interval);
		else
			*count = tlinearseq_extend(result, seq, interval, min);
	}
	else /* temp->duration == TEMPORALS */
	{
		TemporalS *ts = (TemporalS *)temp;
		result = palloc(sizeof(TemporalSeq *) * ts->totalcount);
		if (! MOBDB_FLAGS_GET_LINEAR(temp->flags))
			*count = tsteps_extend(result, ts, interval);
		else
			*count = tlinears_extend(result, ts, interval, min);
	}
	return result;
}

/*****************************************************************************
 * Transform a temporal numeric type into a temporal integer type with value 1 
 * extended by a time interval. 
 *****************************************************************************/

/**
 * Transform the temporal numeric instant value by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] inst Temporal value
 * @param[in] interval Interval
 */
static int
temporalinst_transform_wcount(TemporalSeq **result, TemporalInst *inst, 
	Interval *interval)
{
	TemporalInst *instants[2];
	TimestampTz upper = DatumGetTimestampTz(DirectFunctionCall2(
		timestamptz_pl_interval, TimestampTzGetDatum(inst->t), 
		PointerGetDatum(interval)));
	instants[0] = temporalinst_make(Int32GetDatum(1), inst->t, INT4OID);
	instants[1] = temporalinst_make(Int32GetDatum(1), upper, INT4OID);
	result[0] = temporalseq_make(instants, 2, true, true, 
		false, false);
	pfree(instants[0]);	pfree(instants[1]);
	return 1;
}

/**
 * Transform the temporal numeric instant set value by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] ti Temporal value
 * @param[in] interval Interval
 */
static int
temporali_transform_wcount(TemporalSeq **result, TemporalI *ti, Interval *interval)
{
	for (int i = 0; i < ti->count; i++)
	{
		TemporalInst *inst = temporali_inst_n(ti, i);
		temporalinst_transform_wcount(&result[i], inst, interval);
	}
	return ti->count;
}

/**
 * Transform the temporal numeric sequence value by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] seq Temporal value
 * @param[in] interval Interval
 */
static int
temporalseq_transform_wcount(TemporalSeq **result, TemporalSeq *seq, Interval *interval)
{
	if (seq->count == 1)
		return temporalinst_transform_wcount(result, temporalseq_inst_n(seq, 0), interval);

	TemporalInst *instants[2];
	TemporalInst *inst1 = temporalseq_inst_n(seq, 0);
	bool lower_inc = seq->period.lower_inc;
	for (int i = 0; i < seq->count - 1; i++)
	{
		TemporalInst *inst2 = temporalseq_inst_n(seq, i + 1);
		bool upper_inc = (i == seq->count - 2) ? seq->period.upper_inc : false ;
		TimestampTz upper = DatumGetTimestampTz(DirectFunctionCall2(
			timestamptz_pl_interval, TimestampTzGetDatum(inst2->t), 
			PointerGetDatum(interval)));
		instants[0] = temporalinst_make(Int32GetDatum(1), inst1->t, INT4OID);
		instants[1] = temporalinst_make(Int32GetDatum(1), upper, INT4OID);
		result[i] = temporalseq_make(instants, 2,
			lower_inc, upper_inc, false, false);
		pfree(instants[0]); pfree(instants[1]);
		inst1 = inst2;
		lower_inc = true;
	}	
	return seq->count - 1;
}

/**
 * Transform the temporal numeric sequence set value by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] ts Temporal value
 * @param[in] interval Interval
 */
static int
temporals_transform_wcount(TemporalSeq **result, TemporalS *ts, Interval *interval)
{
	int k = 0;
	for (int i = 0; i < ts->count; i++)
	{
		TemporalSeq *seq = temporals_seq_n(ts, i);
		k += temporalseq_transform_wcount(&result[k], seq, interval);
	}
	return k;
}

/**
 * Transform the temporal numeric value by the time interval (dispatch function)
 *
 * @param[in] temp Temporal value
 * @param[in] interval Interval
 * @param[out] count Number of elements in the output array
 */
static TemporalSeq **
temporal_transform_wcount(Temporal *temp, Interval *interval, int *count)
{
	ensure_valid_duration(temp->duration);
	TemporalSeq **result;
	if (temp->duration == TEMPORALINST)
	{
		TemporalInst *inst = (TemporalInst *)temp;
		result = palloc(sizeof(TemporalSeq *));
		*count = temporalinst_transform_wcount(result, inst, interval);
	}
	else if (temp->duration == TEMPORALI)
	{
		TemporalI *ti = (TemporalI *)temp;
		result = palloc(sizeof(TemporalSeq *) * ti->count);
		*count = temporali_transform_wcount(result, ti, interval);
	}
	else if (temp->duration == TEMPORALSEQ)
	{
		TemporalSeq *seq = (TemporalSeq *)temp;
		result = palloc(sizeof(TemporalSeq *) * seq->count);
		*count = temporalseq_transform_wcount(result, seq, interval);
	}
	else /* temp->duration == TEMPORALS */
	{
		TemporalS *ts = (TemporalS *)temp;
		result = palloc(sizeof(TemporalSeq *) * ts->totalcount);
		*count = temporals_transform_wcount(result, ts, interval);
	}
	return result;
}

/*****************************************************************************/

/**
 * Transform the temporal numeric value into a temporal double and extend it
 * by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] inst Temporal value
 * @param[in] interval Interval
 */
static int
tnumberinst_transform_wavg(TemporalSeq **result, TemporalInst *inst, Interval *interval)
{
	/* Should be additional attribute */
	bool linear = true;
	float8 value = 0.0;
	ensure_numeric_base_type(inst->valuetypid);
	if (inst->valuetypid == INT4OID)
		value = DatumGetInt32(temporalinst_value(inst)); 
	else if (inst->valuetypid == FLOAT8OID)
		value = DatumGetFloat8(temporalinst_value(inst)); 
	double2 dvalue;
	double2_set(&dvalue, value, 1);
	TimestampTz upper = DatumGetTimestampTz(
		DirectFunctionCall2(timestamptz_pl_interval,
		TimestampTzGetDatum(inst->t),
		PointerGetDatum(interval)));
	TemporalInst *instants[2];
	instants[0] = temporalinst_make(PointerGetDatum(&dvalue),
		inst->t, type_oid(T_DOUBLE2));
	instants[1] = temporalinst_make(PointerGetDatum(&dvalue),
		upper, type_oid(T_DOUBLE2));
	result[0] = temporalseq_make(instants, 2,
		true, true, linear, false);
	pfree(instants[0]);	pfree(instants[1]);
	return 1;
}

/**
 * Transform the temporal numeric value into a temporal double and extend it
 * by the time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] ti Temporal value
 * @param[in] interval Interval
 */
static int
tnumberi_transform_wavg(TemporalSeq **result, TemporalI *ti, Interval *interval)
{
	for (int i = 0; i < ti->count; i++)
	{
		TemporalInst *inst = temporali_inst_n(ti, i);
		tnumberinst_transform_wavg(&result[i], inst, interval);
	}
	return ti->count;
}

/**
* Transform the temporal integer sequence value into a temporal double and extend
 * it by a time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] seq Temporal value
 * @param[in] interval Interval
 * @note There is no equivalent function for temporal float types 
 */
static int
tintseq_transform_wavg(TemporalSeq **result, TemporalSeq *seq, Interval *interval)
{
	/* Should be additional attribute */
	bool linear = true;
	TemporalInst *instants[2];
	if (seq->count == 1)
	{
		TemporalInst *inst = temporalseq_inst_n(seq, 0);
		double value = DatumGetInt32(temporalinst_value(inst)); 
		double2 dvalue;
		double2_set(&dvalue, value, 1);
		TimestampTz upper = DatumGetTimestampTz(
			DirectFunctionCall2(timestamptz_pl_interval,
			TimestampTzGetDatum(inst->t),
			PointerGetDatum(interval)));
		instants[0] = temporalinst_make(PointerGetDatum(&dvalue),
			inst->t, type_oid(T_DOUBLE2));
		instants[1] = temporalinst_make(PointerGetDatum(&dvalue),
			upper, type_oid(T_DOUBLE2));
		result[0] = temporalseq_make(instants, 2,
			true, true, linear, false);
		pfree(instants[0]);	pfree(instants[1]);
		return 1;
	}

	TemporalInst *inst1 = temporalseq_inst_n(seq, 0);
	bool lower_inc = seq->period.lower_inc;
	for (int i = 0; i < seq->count - 1; i++)
	{
		TemporalInst *inst2 = temporalseq_inst_n(seq, i + 1);
		bool upper_inc = (i == seq->count - 2) ? seq->period.upper_inc : false ;
		double value = DatumGetInt32(temporalinst_value(inst1)); 
		double2 dvalue;
		double2_set(&dvalue, value, 1);
		TimestampTz upper = DatumGetTimestampTz(DirectFunctionCall2(
			timestamptz_pl_interval, TimestampTzGetDatum(inst2->t),
			PointerGetDatum(interval)));
		instants[0] = temporalinst_make(PointerGetDatum(&dvalue), inst1->t,
			type_oid(T_DOUBLE2));
		instants[1] = temporalinst_make(PointerGetDatum(&dvalue), upper,
			type_oid(T_DOUBLE2));
		result[i] = temporalseq_make(instants, 2,
			lower_inc, upper_inc, linear, false);
		pfree(instants[0]); pfree(instants[1]);
		inst1 = inst2;
		lower_inc = true;
	}
	return seq->count - 1;
}

/**
* Transform the temporal integer sequence set value into a temporal double and extend
 * it by a time interval
 *
 * @param[out] result Array on which the pointers of the newly constructed 
 * values are stored
 * @param[in] ts Temporal value
 * @param[in] interval Interval
 * @note There is no equivalent function for temporal float types 
 */
static int
tints_transform_wavg(TemporalSeq **result, TemporalS *ts, Interval *interval)
{
	int k = 0;
	for (int i = 0; i < ts->count; i++)
	{
		TemporalSeq *seq = temporals_seq_n(ts, i);
		k += tintseq_transform_wavg(&result[k], seq, interval);
	}
	return k;
}

/**
 * Transform the temporal integer sequence set value into a temporal double and extend
 * it by a time interval (dispatch function)
 *
 * @param[in] temp Temporal value
 * @param[in] interval Interval
 * @param[out] count Number of elements in the output array
 * @note There is no equivalent function for temporal float types 
*/
static TemporalSeq **
tnumber_transform_wavg(Temporal *temp, Interval *interval, int *count)
{
	ensure_valid_duration(temp->duration);
	TemporalSeq **result;
	if (temp->duration == TEMPORALINST)
	{	
		TemporalInst *inst = (TemporalInst *)temp;
		result = palloc(sizeof(TemporalSeq *));
		*count = tnumberinst_transform_wavg(result, inst, interval);
	}
	else if (temp->duration == TEMPORALI)
	{	
		TemporalI *ti = (TemporalI *)temp;
		result = palloc(sizeof(TemporalSeq *) * ti->count);
		*count = tnumberi_transform_wavg(result, ti, interval);
	}
	else if (temp->duration == TEMPORALSEQ)
	{
		TemporalSeq *seq = (TemporalSeq *)temp;
		result = palloc(sizeof(TemporalSeq *) * seq->count);
		*count = tintseq_transform_wavg(result, seq, interval);
	}
	else /* temp->duration == TEMPORALS */
	{
		TemporalS *ts = (TemporalS *)temp;
		result = palloc(sizeof(TemporalSeq *) * ts->totalcount);
		*count = tints_transform_wavg(result, ts, interval);
	}
	return result;
}

/*****************************************************************************
 * Generic moving window transition functions 
 *****************************************************************************/

/**
 * Generic moving window transition function for min, max, and sum aggregation
 *
 * @param[in] fcinfo Catalog information about the external function
 * @param[inout] state Skiplist containing the state
 * @param[in] temp Temporal value
 * @param[in] interval Interval
 * @param[in] func Function
 * @param[in] min True if the calling function is min (max otherwise)
 * @param[in] crossings State whether turning points are added in the segments
 * @note This function is directly called by the window sum aggregation for 
 * temporal floats after verifying since the operation is not supported for 
 * sequence (set) duration
 */
static SkipList *
temporal_wagg_transfn1(FunctionCallInfo fcinfo, SkipList *state, 
	Temporal *temp, Interval *interval,
	Datum (*func)(Datum, Datum), bool min, bool crossings)
{
	int count;
	TemporalSeq **sequences = temporal_extend(temp, interval, min, &count);
	SkipList *result = temporalseq_tagg_transfn(fcinfo, state, sequences[0], 
		func, crossings);
	for (int i = 1; i < count; i++)
		result = temporalseq_tagg_transfn(fcinfo, result, sequences[i],
			func, crossings);
	for (int i = 0; i < count; i++)
		pfree(sequences[i]);
	pfree(sequences);
	return result;
}

/**
 * Generic moving window transition function for min, max, and sum aggregation
 *
 * @param[in] fcinfo Catalog information about the external function
 * @param[in] func Function
 * @param[in] min True if the calling function is min (max otherwise)
 * @param[in] crossings State whether turning points are added in the segments
 */
Datum
temporal_wagg_transfn(FunctionCallInfo fcinfo, 
	Datum (*func)(Datum, Datum), bool min, bool crossings)
{
	SkipList *state = PG_ARGISNULL(0) ? NULL :
		(SkipList *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1) || PG_ARGISNULL(2))
	{
		if (! state)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state);
	}
	Temporal *temp = PG_GETARG_TEMPORAL(1);
	Interval *interval = PG_GETARG_INTERVAL_P(2);

	SkipList *result = temporal_wagg_transfn1(fcinfo, state, temp, interval, func,
		min, crossings);
	
	PG_FREE_IF_COPY(temp, 1);
	PG_FREE_IF_COPY(interval, 2);
	PG_RETURN_POINTER(result);
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(tint_wmin_transfn);
/**
 * Transition function for moving window minimun aggregation for temporal integer values
 */
PGDLLEXPORT Datum
tint_wmin_transfn(PG_FUNCTION_ARGS)
{
	return temporal_wagg_transfn(fcinfo, &datum_min_int32, true, true);
}

PG_FUNCTION_INFO_V1(tfloat_wmin_transfn);
/**
 * Transition function for moving window minimun
 */
PGDLLEXPORT Datum
tfloat_wmin_transfn(PG_FUNCTION_ARGS)
{
	return temporal_wagg_transfn(fcinfo, &datum_min_float8, true, true);
}

PG_FUNCTION_INFO_V1(tint_wmax_transfn);
/**
 * Transition function for moving window maximun aggregation for temporal integer values
 */
PGDLLEXPORT Datum
tint_wmax_transfn(PG_FUNCTION_ARGS)
{
	return temporal_wagg_transfn(fcinfo, &datum_max_int32, false, true);
}

PG_FUNCTION_INFO_V1(tfloat_wmax_transfn);
/**
 * Transition function for moving window maximun aggregation for temporal float values
 */
PGDLLEXPORT Datum
tfloat_wmax_transfn(PG_FUNCTION_ARGS)
{
	return temporal_wagg_transfn(fcinfo, &datum_max_float8, false, true);
}

PG_FUNCTION_INFO_V1(tint_wsum_transfn);
/**
 * Transition function for moving window sum aggregation for temporal inter values
 */
PGDLLEXPORT Datum
tint_wsum_transfn(PG_FUNCTION_ARGS)
{
	return temporal_wagg_transfn(fcinfo, &datum_sum_int32, true, false);
}

PG_FUNCTION_INFO_V1(tfloat_wsum_transfn);
/**
 * Transition function for moving window sum aggregation for temporal float values
 */
PGDLLEXPORT Datum
tfloat_wsum_transfn(PG_FUNCTION_ARGS)
{
	SkipList *state = PG_ARGISNULL(0) ? NULL :
		(SkipList *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1) || PG_ARGISNULL(2))
	{
		if (! state)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state);
	}
	Temporal *temp = PG_GETARG_TEMPORAL(1);
	if ((temp->duration == TEMPORALSEQ || temp->duration == TEMPORALS) &&
		temp->valuetypid == FLOAT8OID)
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Operation not supported for temporal float sequences")));
	Interval *interval = PG_GETARG_INTERVAL_P(2);
	SkipList *result = temporal_wagg_transfn1(fcinfo, state, temp, interval, 
		&datum_sum_float8, true, false);
	PG_FREE_IF_COPY(temp, 1);
	PG_FREE_IF_COPY(interval, 2);
	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(temporal_wcount_transfn);
/**
 * Transition function for moving window count aggregation for temporal values
 */
PGDLLEXPORT Datum
temporal_wcount_transfn(PG_FUNCTION_ARGS)
{
	SkipList *state = PG_ARGISNULL(0) ? NULL :
		(SkipList *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1) || PG_ARGISNULL(2))
	{
		if (! state)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state);
	}
	Temporal *temp = PG_GETARG_TEMPORAL(1);
	Interval *interval = PG_GETARG_INTERVAL_P(2);
	int count;
	TemporalSeq **sequences = temporal_transform_wcount(temp, interval, &count);
	SkipList *result = temporalseq_tagg_transfn(fcinfo, state, sequences[0], 
		&datum_sum_int32, false);
	for (int i = 1; i < count; i++)
		result = temporalseq_tagg_transfn(fcinfo, result, sequences[i], 
			&datum_sum_int32, false);
	for (int i = 0; i < count; i++)
		pfree(sequences[i]);
	pfree(sequences);
	PG_FREE_IF_COPY(temp, 1);
	PG_FREE_IF_COPY(interval, 2);
	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(tnumber_wavg_transfn);
/**
 * Transition function for moving window average aggregation for temporal values
 */
PGDLLEXPORT Datum
tnumber_wavg_transfn(PG_FUNCTION_ARGS)
{
	SkipList *state = PG_ARGISNULL(0) ? NULL :
		(SkipList *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1) || PG_ARGISNULL(2))
	{
		if (! state)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state);
	}
	Temporal *temp = PG_GETARG_TEMPORAL(1);
	Interval *interval = PG_GETARG_INTERVAL_P(2);
	int count;
	TemporalSeq **sequences = tnumber_transform_wavg(temp, interval, &count);
	SkipList *result = temporalseq_tagg_transfn(fcinfo, state, sequences[0], 
		&datum_sum_double2, false);
	for (int i = 1; i < count; i++)
		result = temporalseq_tagg_transfn(fcinfo, result, sequences[i], 
			&datum_sum_double2, false);
	for (int i = 0; i < count; i++)
		pfree(sequences[i]);
	pfree(sequences);
	PG_FREE_IF_COPY(temp, 1);
	PG_FREE_IF_COPY(interval, 2);
	PG_RETURN_POINTER(result);
}

/*****************************************************************************/
