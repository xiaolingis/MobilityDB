/*****************************************************************************
 *
 * TemporalPoint.h
 *	  Functions for temporal points.
 *
 * Portions Copyright (c) 2019, Esteban Zimanyi, Arthur Lesuisse, 
 * 		Universite Libre de Bruxelles
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *****************************************************************************/

#ifndef __TEMPORALPOINT_H__
#define __TEMPORALPOINT_H__

#include "TemporalTypes.h"
#include <liblwgeom.h>
#include "PostGIS.h"

/*****************************************************************************
 * Macros for manipulating the 'typmod' int. An int32_t used as follows:
 * Plus/minus = Top bit.
 * Spare bits = Next 2 bits.
 * SRID = Next 21 bits.
 * TYPE = Next 6 bits.
 * ZM Flags = Bottom 2 bits.
 *****************************************************************************/

/* The following (commented out) definitions are taken from POSTGIS
#define TYPMOD_GET_SRID(typmod) ((((typmod) & 0x0FFFFF00) - ((typmod) & 0x10000000)) >> 8)
#define TYPMOD_SET_SRID(typmod, srid) ((typmod) = (((typmod) & 0xE00000FF) | ((srid & 0x001FFFFF)<<8)))
#define TYPMOD_GET_TYPE(typmod) ((typmod & 0x000000FC)>>2)
#define TYPMOD_SET_TYPE(typmod, type) ((typmod) = (typmod & 0xFFFFFF03) | ((type & 0x0000003F)<<2))
#define TYPMOD_GET_Z(typmod) ((typmod & 0x00000002)>>1)
#define TYPMOD_SET_Z(typmod) ((typmod) = typmod | 0x00000002)
#define TYPMOD_GET_M(typmod) (typmod & 0x00000001)
#define TYPMOD_SET_M(typmod) ((typmod) = typmod | 0x00000001)
#define TYPMOD_GET_NDIMS(typmod) (2+TYPMOD_GET_Z(typmod)+TYPMOD_GET_M(typmod))
*/

/* In order to reuse the above (commented out) macros for manipulating the
   typmod from POSTGIS we need to shift them to take into account that the 
   first 4 bits are taken for the duration type */

#define TYPMOD_DEL_DURATION(typmod) (typmod = typmod >> 4 )
#define TYPMOD_SET_DURATION(typmod, durtype) ((typmod) = typmod << 4 | durtype)


/*****************************************************************************
 * STBOX macros
 *****************************************************************************/

#define DatumGetSTboxP(X)    ((STBOX *) DatumGetPointer(X))
#define STboxPGetDatum(X)    PointerGetDatum(X)
#define PG_GETARG_STBOX_P(n) DatumGetSTboxP(PG_GETARG_DATUM(n))
#define PG_RETURN_STBOX_P(x) return STboxPGetDatum(x)

/*****************************************************************************
 * Parsing routines: File Parser.c
 *****************************************************************************/

extern STBOX *stbox_parse(char **str);
extern Temporal *tpoint_parse(char **str, Oid basetype);

/*****************************************************************************
 * STBOX routines: File STbox.c
 *****************************************************************************/

extern Datum stbox_in(PG_FUNCTION_ARGS);
extern Datum stbox_out(PG_FUNCTION_ARGS);
extern Datum stbox_constructor(PG_FUNCTION_ARGS);
extern Datum stboxt_constructor(PG_FUNCTION_ARGS);
extern Datum geodstbox_constructor(PG_FUNCTION_ARGS);

extern STBOX *stbox_new(bool hasx, bool hasz, bool hast, bool geodetic);
extern STBOX *stbox_copy(const STBOX *box);
extern int stbox_cmp_internal(const STBOX *box1, const STBOX *box2);

/*****************************************************************************
 * Miscellaneous functions defined in TemporalPoint.c
 *****************************************************************************/

extern void temporalgeom_init();

/* Input/output functions */

extern Datum tpoint_in(PG_FUNCTION_ARGS);

/* Accessor functions */

extern Datum tpoint_value(PG_FUNCTION_ARGS);
extern Datum tpoint_values(PG_FUNCTION_ARGS);
extern Datum tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum tpoint_ever_equals(PG_FUNCTION_ARGS);
extern Datum tpoint_always_equals(PG_FUNCTION_ARGS);

extern Datum tpoint_values_internal(Temporal *temp);

extern bool tpointinst_ever_equals(TemporalInst *inst, GSERIALIZED *value);

extern Datum tgeompointi_values(TemporalI *ti);
extern Datum tgeogpointi_values(TemporalI *ti);
extern Datum tpointi_values(TemporalI *ti);

/* Restriction functions */

extern Datum tpoint_at_value(PG_FUNCTION_ARGS);
extern Datum tpoint_minus_value(PG_FUNCTION_ARGS);
extern Datum tpoint_at_values(PG_FUNCTION_ARGS);
extern Datum tpoint_minus_values(PG_FUNCTION_ARGS);
extern Datum tpoints_at_values(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Spatial functions defined in SpatialFuncs.c
 *****************************************************************************/

extern POINT2D gs_get_point2d(GSERIALIZED *gs);
extern POINT3DZ gs_get_point3dz(GSERIALIZED *gs);
extern POINT3DM gs_get_point3dm(GSERIALIZED *gs);
extern POINT4D gs_get_point4d(GSERIALIZED *gs);
extern POINT2D datum_get_point2d(Datum value);
extern POINT3DZ datum_get_point3dz(Datum value);
extern bool datum_point_eq(Datum geopoint1, Datum geopoint2);
extern void tpoint_same_srid(Temporal *temp1, Temporal *temp2);
extern void tpoint_gs_same_srid(Temporal *temp, GSERIALIZED *gs);
extern void tpoint_same_dimensionality(Temporal *temp1, Temporal *temp2);
extern void tpoint_gs_same_dimensionality(Temporal *temp, GSERIALIZED *gs);
extern void tpoint_check_Z_dimension(Temporal *temp1, Temporal *temp2);
extern void tpoint_gs_check_Z_dimension(Temporal *temp, GSERIALIZED *gs);
extern void gserialized_check_point(GSERIALIZED *gs);
extern void gserialized_check_M_dimension(GSERIALIZED *gs);
extern GSERIALIZED* geometry_serialize(LWGEOM* geom);

/* Functions for output in WKT and EWKT format */

extern Datum tpoint_as_text(PG_FUNCTION_ARGS);
extern Datum tpoint_as_ewkt(PG_FUNCTION_ARGS);
extern Datum geoarr_as_text(PG_FUNCTION_ARGS);
extern Datum geoarr_as_ewkt(PG_FUNCTION_ARGS);
extern Datum tpointarr_as_text(PG_FUNCTION_ARGS);
extern Datum tpointarr_as_ewkt(PG_FUNCTION_ARGS);

/* Functions for spatial reference systems */

extern Datum tpoint_srid(PG_FUNCTION_ARGS);
extern Datum tpoint_set_srid(PG_FUNCTION_ARGS);
extern Datum tgeompoint_transform(PG_FUNCTION_ARGS);

extern Temporal* tpoint_set_srid_internal(Temporal* temp, int32 srid) ;
extern int tpoint_srid_internal(Temporal *t);
extern TemporalInst *tgeompointinst_transform(TemporalInst *inst, Datum srid);

/* Cast functions */

extern Datum tgeompoint_as_tgeogpoint(PG_FUNCTION_ARGS);
extern Datum tgeogpoint_as_tgeompoint(PG_FUNCTION_ARGS);

extern TemporalInst *tgeogpointinst_as_tgeompointinst(TemporalInst *inst);
extern TemporalI *tgeogpointi_as_tgeompointi(TemporalI *ti);
extern TemporalSeq *tgeogpointseq_as_tgeompointseq(TemporalSeq *seq);
extern TemporalS *tgeogpoints_as_tgeompoints(TemporalS *ts);

/* Trajectory functions */

extern Datum tpoint_trajectory(PG_FUNCTION_ARGS);

extern Datum tpointseq_make_trajectory(TemporalInst **instants, int count);
extern Datum tpointseq_trajectory_append(TemporalSeq *seq, TemporalInst *inst, bool replace);

extern Datum geompoint_trajectory(Datum value1, Datum value2);
extern Datum tgeogpointseq_trajectory1(TemporalInst *inst1, TemporalInst *inst2);

extern Datum tpointseq_trajectory(TemporalSeq *seq);
extern Datum tpointseq_trajectory_copy(TemporalSeq *seq);
extern Datum tpoints_trajectory(TemporalS *ts);

/* Length, speed, time-weighted centroid, and temporal azimuth functions */

extern Datum tpoint_length(PG_FUNCTION_ARGS);
extern Datum tpoint_cumulative_length(PG_FUNCTION_ARGS);
extern Datum tpoint_speed(PG_FUNCTION_ARGS);
extern Datum tgeompoint_twcentroid(PG_FUNCTION_ARGS);
extern Datum tpoint_azimuth(PG_FUNCTION_ARGS);

extern Datum tgeompointi_twcentroid(TemporalI *ti);
extern Datum tgeompointseq_twcentroid(TemporalSeq *seq);
extern Datum tgeompoints_twcentroid(TemporalS *ts);

/* Restriction functions */

extern Datum tpoint_at_geometry(PG_FUNCTION_ARGS);
extern Datum tpoint_minus_geometry(PG_FUNCTION_ARGS);

extern TemporalSeq **tpointseq_at_geometry2(TemporalSeq *seq, Datum geo, int *count);

/* Nearest approach functions */

extern Datum NAI_geometry_tpoint(PG_FUNCTION_ARGS);
extern Datum NAI_tpoint_geometry(PG_FUNCTION_ARGS);
extern Datum NAI_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum NAD_geometry_tpoint(PG_FUNCTION_ARGS);
extern Datum NAD_tpoint_geometry(PG_FUNCTION_ARGS);
extern Datum NAD_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum shortestline_geometry_tpoint(PG_FUNCTION_ARGS);
extern Datum shortestline_tpoint_geometry(PG_FUNCTION_ARGS);
extern Datum shortestline_tpoint_tpoint(PG_FUNCTION_ARGS);

/* Functions converting a temporal point to/from a PostGIS trajectory */

extern Datum tpoint_to_geo(PG_FUNCTION_ARGS);
extern Datum geo_to_tpoint(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Restriction functions defined in ProjectionGK.c
 *****************************************************************************/

extern Datum tgeompoint_transform_gk(PG_FUNCTION_ARGS);
extern Datum geometry_transform_gk(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Geometric aggregate functions defined in GeoAggFuncs.c
 *****************************************************************************/

extern Datum tpoint_tcentroid_transfn(PG_FUNCTION_ARGS);
extern Datum tpoint_tcentroid_combinefn(PG_FUNCTION_ARGS);
extern Datum tpoint_tcentroid_finalfn(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Spatial relationship functions defined in SpatialRels.c
 *****************************************************************************/

extern Datum geom_contains(Datum geom1, Datum geom2);
extern Datum geom_containsproperly(Datum geom1, Datum geom2);
extern Datum geom_covers(Datum geom1, Datum geom2);
extern Datum geom_coveredby(Datum geom1, Datum geom2);
extern Datum geom_crosses(Datum geom1, Datum geom2);
extern Datum geom_disjoint(Datum geom1, Datum geom2);
extern Datum geom_equals(Datum geom1, Datum geom2);
extern Datum geom_intersects2d(Datum geom1, Datum geom2);
extern Datum geom_intersects3d(Datum geom1, Datum geom2);
extern Datum geom_overlaps(Datum geom1, Datum geom2);
extern Datum geom_touches(Datum geom1, Datum geom2);
extern Datum geom_within(Datum geom1, Datum geom2);
extern Datum geom_dwithin2d(Datum geom1, Datum geom2, Datum dist);
extern Datum geom_dwithin3d(Datum geom1, Datum geom2, Datum dist);
extern Datum geom_relate(Datum geom1, Datum geom2);
extern Datum geom_relate_pattern(Datum geom1, Datum geom2, Datum pattern);

extern Datum geog_covers(Datum geog1, Datum geog2);
extern Datum geog_coveredby(Datum geog1, Datum geog2);
extern Datum geog_disjoint(Datum geog1, Datum geog2);
extern Datum geog_intersects(Datum geog1, Datum geog2);
extern Datum geog_dwithin(Datum geog1, Datum geog2, Datum dist);

extern Datum contains_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum contains_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum contains_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum containsproperly_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum containsproperly_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum containsproperly_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum covers_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum covers_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum covers_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum coveredby_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum coveredby_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum coveredby_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum crosses_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum crosses_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum crosses_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum disjoint_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum disjoint_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum disjoint_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum equals_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum equals_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum equals_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum intersects_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum intersects_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum intersects_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum overlaps_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum overlaps_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum overlaps_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum touches_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum touches_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum touches_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum within_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum within_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum within_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum dwithin_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum dwithin_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum dwithin_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum relate_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum relate_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum relate_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum relate_pattern_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum relate_pattern_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum relate_pattern_tpoint_tpoint(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Temporal spatial relationship functions defined in TempSpatialRels.c
 *****************************************************************************/

extern Datum tcontains_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tcontains_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tcontains_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum tcontainsproperly_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tcontainsproperly_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tcontainsproperly_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum tcovers_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tcovers_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tcovers_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum tcoveredby_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tcoveredby_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tcoveredby_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum tdisjoint_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tdisjoint_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tdisjoint_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum tequals_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tequals_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tequals_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum tintersects_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tintersects_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tintersects_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum ttouches_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum ttouches_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum ttouches_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum twithin_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum twithin_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum twithin_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum tdwithin_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tdwithin_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tdwithin_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum trelate_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum trelate_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum trelate_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum trelate_pattern_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum trelate_pattern_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum trelate_pattern_tpoint_tpoint(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Bounding box operators defined in BoundBoxOps.c
 *****************************************************************************/

/* STBOX functions */

extern Datum contains_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum contained_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overlaps_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum same_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum distance_stbox_stbox(PG_FUNCTION_ARGS);

extern bool contains_stbox_stbox_internal(const STBOX *box1, const STBOX *box2);
extern bool contained_stbox_stbox_internal(const STBOX *box1, const STBOX *box2);
extern bool overlaps_stbox_stbox_internal(const STBOX *box1, const STBOX *box2);
extern bool same_stbox_stbox_internal(const STBOX *box1, const STBOX *box2);
extern double distance_stbox_stbox_internal(STBOX *box1, STBOX *box2);

/* Functions computing the bounding box at the creation of the temporal point */

extern void tpointinst_make_stbox(STBOX *box, Datum value, TimestampTz t);
extern void tpointinstarr_to_stbox(STBOX *box, TemporalInst **inst, int count);
extern void tpointseqarr_to_stbox(STBOX *box, TemporalSeq **seq, int count);

extern void tpoint_expand_stbox(STBOX *box, Temporal *temp, TemporalInst *inst);

/* Functions for expanding the bounding box */

extern Datum stbox_expand_spatial(PG_FUNCTION_ARGS);
extern Datum tpoint_expand_spatial(PG_FUNCTION_ARGS);
extern Datum stbox_expand_temporal(PG_FUNCTION_ARGS);
extern Datum tpoint_expand_temporal(PG_FUNCTION_ARGS);

/* Transform a <Type> to a STBOX */

extern Datum geo_to_stbox(PG_FUNCTION_ARGS);
extern Datum geo_timestamp_to_stbox(PG_FUNCTION_ARGS);
extern Datum geo_period_to_stbox(PG_FUNCTION_ARGS);

extern bool geo_to_stbox_internal(STBOX *box, GSERIALIZED *gs);
extern void timestamp_to_stbox_internal(STBOX *box, TimestampTz t);
extern void timestampset_to_stbox_internal(STBOX *box, TimestampSet *ps);
extern void period_to_stbox_internal(STBOX *box, Period *p);
extern void periodset_to_stbox_internal(STBOX *box, PeriodSet *ps);
extern bool geo_timestamp_to_stbox_internal(STBOX *box, GSERIALIZED* geom, TimestampTz t);
extern bool geo_period_to_stbox_internal(STBOX *box, GSERIALIZED* geom, Period *p);

/*****************************************************************************/

extern Datum overlaps_bbox_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum overlaps_bbox_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overlaps_bbox_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum overlaps_bbox_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overlaps_bbox_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum contains_bbox_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum contains_bbox_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum contains_bbox_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum contains_bbox_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum contains_bbox_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum contained_bbox_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum contained_bbox_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum contained_bbox_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum contained_bbox_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum contained_bbox_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Datum same_bbox_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum same_bbox_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum same_bbox_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum same_bbox_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum same_bbox_tpoint_tpoint(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Relative position functions defined in RelativePosOpsM.c
 *****************************************************************************/

extern Datum left_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overleft_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum right_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overright_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum below_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overbelow_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum above_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overabove_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum front_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overfront_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum back_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overback_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum before_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overbefore_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum after_stbox_stbox(PG_FUNCTION_ARGS);
extern Datum overafter_stbox_stbox(PG_FUNCTION_ARGS);

extern bool left_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overleft_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool right_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overright_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool below_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overbelow_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool above_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overabove_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool front_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overfront_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool back_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overback_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool before_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overbefore_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool after_stbox_stbox_internal(STBOX *box1, STBOX *box2);
extern bool overafter_stbox_stbox_internal(STBOX *box1, STBOX *box2);

extern Datum left_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum overleft_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum right_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum overright_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum below_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum overbelow_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum above_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum overabove_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum front_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum overfront_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum back_geom_tpoint(PG_FUNCTION_ARGS);
extern Datum overback_geom_tpoint(PG_FUNCTION_ARGS);

extern Datum left_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum overleft_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum right_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum overright_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum above_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum overabove_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum below_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum overbelow_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum front_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum overfront_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum back_tpoint_geom(PG_FUNCTION_ARGS);
extern Datum overback_tpoint_geom(PG_FUNCTION_ARGS);

extern Datum left_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overleft_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum right_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overright_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum below_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overbelow_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum above_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overabove_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum front_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overfront_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum back_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overback_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum before_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overbefore_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum after_stbox_tpoint(PG_FUNCTION_ARGS);
extern Datum overafter_stbox_tpoint(PG_FUNCTION_ARGS);

extern Datum left_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overleft_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum right_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overright_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum above_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overabove_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum below_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overbelow_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum front_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overfront_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum back_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overback_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum before_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overbefore_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum after_tpoint_stbox(PG_FUNCTION_ARGS);
extern Datum overafter_tpoint_stbox(PG_FUNCTION_ARGS);

extern Datum left_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overleft_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum right_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overright_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum above_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overabove_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum below_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overbelow_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum front_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overfront_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum back_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overback_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum before_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overbefore_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum after_tpoint_tpoint(PG_FUNCTION_ARGS);
extern Datum overafter_tpoint_tpoint(PG_FUNCTION_ARGS);

/*****************************************************************************
 * Temporal distance functions defined in TempDistance.c
 *****************************************************************************/

extern Datum geom_distance2d(Datum geom1, Datum geom2);
extern Datum geom_distance3d(Datum geom1, Datum geom2);
extern Datum geog_distance(Datum geog1, Datum geog2);

extern Datum distance_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum distance_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum distance_tpoint_tpoint(PG_FUNCTION_ARGS);

extern Temporal *distance_tpoint_tpoint_internal(Temporal *temp1, Temporal *temp2);

/*****************************************************************************
 * Index functions defined in IndexGistTPoint.c
 *****************************************************************************/

extern Datum gist_tpoint_consistent(PG_FUNCTION_ARGS);
extern Datum gist_tpoint_union(PG_FUNCTION_ARGS);
extern Datum gist_tpoint_penalty(PG_FUNCTION_ARGS);
extern Datum gist_tpoint_picksplit(PG_FUNCTION_ARGS);
extern Datum gist_tpoint_same(PG_FUNCTION_ARGS);
extern Datum gist_tpoint_compress(PG_FUNCTION_ARGS);
extern Datum gist_tpoint_decompress(PG_FUNCTION_ARGS);
extern Datum gist_tpoint_distance(PG_FUNCTION_ARGS);

/* The following functions are also called by IndexSpgistTPoint.c */
extern bool index_tpoint_bbox_recheck(StrategyNumber strategy);
extern bool index_leaf_consistent_stbox_box2D(STBOX *key, STBOX *query, 
	StrategyNumber strategy);
extern bool index_leaf_consistent_stbox_period(STBOX *key, Period *query, 
	StrategyNumber strategy);
extern bool index_leaf_consistent_stbox_stbox(STBOX *key, STBOX *query, 
	StrategyNumber strategy);

extern bool index_tpoint_recheck(StrategyNumber strategy);
extern bool index_leaf_consistent_stbox(STBOX *key, STBOX *query, 
	StrategyNumber strategy);

/*****************************************************************************
 * Index functions defined in IndexSpgistTPoint.c
 *****************************************************************************/

extern Datum spgist_tgeogpoint_config(PG_FUNCTION_ARGS);
extern Datum spgist_tpoint_choose(PG_FUNCTION_ARGS);
extern Datum spgist_tpoint_picksplit(PG_FUNCTION_ARGS);
extern Datum spgist_tpoint_inner_consistent(PG_FUNCTION_ARGS);
extern Datum spgist_tpoint_leaf_consistent(PG_FUNCTION_ARGS);

/*****************************************************************************/

#endif
