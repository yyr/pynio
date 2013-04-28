#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#ifdef NIO_LIB_ONLY
#include "niohlu.h"
#include "nioNresDB.h"
#include "nioCallbacks.h"
#else
#include <ncarg/hlu/hlu.h>
#include <ncarg/hlu/NresDB.h>
#include <ncarg/hlu/Callbacks.h>
#endif
#include "defs.h"
#include <netcdf.h>
#include "NclDataDefs.h"
#include "NclFileInterfaces.h"
#include "NclMdInc.h"
#include "DataSupport.h"
#include "date.h"
#include "NclGRIB.h"
#include "tables.h"
#include "cptec_254_gtb.h"
#include "dwd_002_gtb.h"
#include "dwd_201_gtb.h"
#include "dwd_202_gtb.h"
#include "dwd_203_gtb.h"
#include "dwd_204_gtb.h"
#include "dwd_205_gtb.h"
#include "dwd_206_gtb.h"
#include "dwd_207_gtb.h"
#include "ecmwf_128_gtb.h"
#include "ecmwf_129_gtb.h"
#include "ecmwf_130_gtb.h"
#include "ecmwf_131_gtb.h"
#include "ecmwf_132_gtb.h"
#include "ecmwf_133_gtb.h"
#include "ecmwf_140_gtb.h"
#include "ecmwf_150_gtb.h"
#include "ecmwf_151_gtb.h"
#include "ecmwf_160_gtb.h"
#include "ecmwf_162_gtb.h"
#include "ecmwf_170_gtb.h"
#include "ecmwf_171_gtb.h"
#include "ecmwf_172_gtb.h"
#include "ecmwf_173_gtb.h"
#include "ecmwf_174_gtb.h"
#include "ecmwf_175_gtb.h"
#include "ecmwf_180_gtb.h"
#include "ecmwf_190_gtb.h"
#include "ecmwf_200_gtb.h"
#include "ecmwf_201_gtb.h"
#include "ecmwf_210_gtb.h"
#include "ecmwf_211_gtb.h"
#include "ecmwf_228_gtb.h"
#include "ecmwf_230_gtb.h"
#include "ecmwf_234_gtb.h"
#include "ncep_opn_gtb.h"
#include "ncep_reanal_gtb.h"
#include "ncep_128_gtb.h"
#include "ncep_129_gtb.h"
#include "ncep_130_gtb.h"
#include "ncep_131_gtb.h"
#include "ncep_133_gtb.h"
#include "ncep_140_gtb.h"
#include "ncep_141_gtb.h"
#include "fsl0_gtb.h"
#include "fsl1_gtb.h"
#include "fsl2_gtb.h"
#include "fnmoc_gtb.h"
#include "jma_3_gtb.h"

#define NCL_GRIB_CACHE_SIZE  150
/* 
 * These are the codes in ON388 - Table 4 - for time units arranged in order from 
 * short to long duration. The convert table below is the conversion from
 * the shortest duration (second) to each of the other units. (For periods longer
 * than a day there is inaccuracy because the periods vary depending on which which 
 * month and/or year we are talking about. For now use average based on 365.25 days per year.
 * This will need to be refined. This is for comparision of time periods in common units.
 * 
 */


static int Unit_Code_Order[] = { 254,0,13,14,1,10,11,12,2,3,4,5,6,7 };
static double Unit_Convert[] = { 1.0, 60.0, 900.0,1800.0,3600.0, 10800.0, 21600.0, /* 1 sec - 6 hr */
			  43200.0,86400.0,2629800.0, 31557600.0, /* 12 hr - 1 yr */
			  315576000.0,946728000.0,3155760000.0};   /* 10 yr - 100 yr */

/*
 * this is a hack function that applies to GRID TYPE 203 only
 * according to Mike Baldwin <baldwin@nssl.noaa.gov> the parameters listed 
 * (NCEP operational table assumed) are on the UV grid. All others
 * are on the mass grid.
 *
 */
extern int Is_UV(int param_number) {
	switch (param_number) {
	case 33:
	case 34:
	case 190:
	case 196:
	case 197:
		return 1;
	default:
		return 0;
	}
}

extern int grid_index[];
extern int grid_gds_index[];
extern GridInfoRecord grid[];
extern GridGDSInfoRecord grid_gds[];
extern int grid_tbl_len;
extern int grid_gds_tbl_len;
static unsigned char  *vbuf;
static int vbuflen;
static int vbufpos;
static PtableInfo *Ptables = NULL;

extern void GribPushAtt(
#if NhlNeedProto
GribAttInqRecList **att_list_ptr,char* name,void *val,ng_size_t dimsize,NclObjClass type
#endif
);

static void _AdjustCacheTypeAndMissing
#if NhlNeedProto
(int int_or_float,NclMultiDValData the_dat,NclScalar *missingv)
#else
(int_or_float,the_dat,missingv)
(int int_or_float,NclMultiDValData the_dat,NclScalar *missingv)
#endif
{
	if(int_or_float) {
		the_dat->multidval.hlu_type_rep[0] = ((NclTypeintClass)nclTypeintClass)->type_class.hlu_type_rep[0];
		the_dat->multidval.hlu_type_rep[1] = ((NclTypeintClass)nclTypeintClass)->type_class.hlu_type_rep[1];
		the_dat->multidval.data_type = ((NclTypeintClass)nclTypeintClass)->type_class.data_type;
		the_dat->multidval.type = (NclTypeClass)nclTypeintClass;
		if(missingv != NULL) {
			the_dat->multidval.missing_value.has_missing = 1;
			the_dat->multidval.missing_value.value = *missingv;
		} else {
			the_dat->multidval.missing_value.has_missing = 0;
		}
	} else {
/*
* Type is float by defaul/
*/
		if(missingv != NULL) {
			the_dat->multidval.missing_value.has_missing = 1;
			the_dat->multidval.missing_value.value = *missingv;
		} else {
			the_dat->multidval.missing_value.has_missing = 0;
		}
	}
}
static void _NewGridCache
#if NhlNeedProto
(GribFileRecord *therec,GribParamList *step)
#else
(therec,step)
GribFileRecord *therec;
GribParamList *step
#endif
{
	NclGribCacheList *newlist;
	int nvar_dims;

	if(therec->grib_grid_cache == NULL) {
		therec->grib_grid_cache = NclMalloc(sizeof(NclGribCacheList));
		newlist = NULL;
	} else {
		newlist = therec->grib_grid_cache;
                therec->grib_grid_cache = NclMalloc(sizeof(NclGribCacheList));
	}
		
	therec->grib_grid_cache->grid_number = step->grid_number;
	therec->grib_grid_cache->has_gds = step->has_gds;
	therec->grib_grid_cache->grid_gds_tbl_index = step->grid_gds_tbl_index;
	therec->grib_grid_cache->n_dims = 2;
	nvar_dims = step->var_info.num_dimensions;
	therec->grib_grid_cache->dimsizes[0] = step->var_info.dim_sizes[nvar_dims - 2];
	therec->grib_grid_cache->dimsizes[1] = step->var_info.dim_sizes[nvar_dims - 1];
	therec->grib_grid_cache->dim_ids[0] = step->var_info.file_dim_num[nvar_dims - 2];
	therec->grib_grid_cache->dim_ids[1] = step->var_info.file_dim_num[nvar_dims - 1];
	therec->grib_grid_cache->int_missing_rec = NULL;
	therec->grib_grid_cache->float_missing_rec = NULL;

#if 0
	/* i don't think there are any grib grids where one of the coordinates is 2d and the other is 1d */
	} else if ((n_dims_lon ==2) &&(n_dims_lat ==2)) {
		therec->grib_grid_cache->n_dims = 2;
		therec->grib_grid_cache->dimsizes[1] = dimsizes_lon[1];
		therec->grib_grid_cache->dimsizes[0] = dimsizes_lat[0];
	} else {
		if(n_dims_lat ==2) {
			therec->grib_grid_cache->n_dims = 2;
			therec->grib_grid_cache->dimsizes[1] = dimsizes_lat[1];
			therec->grib_grid_cache->dimsizes[0] = dimsizes_lat[0];
		} else if(n_dims_lon ==2) {
			therec->grib_grid_cache->n_dims = 2;
			therec->grib_grid_cache->dimsizes[1] = dimsizes_lon[1];
			therec->grib_grid_cache->dimsizes[0] = dimsizes_lon[0];
		}
	}
#endif
	therec->grib_grid_cache->n_entries = 0;
	therec->grib_grid_cache->thelist = NULL;
	therec->grib_grid_cache->next = newlist;
}
static void _NewSHGridCache
#if NhlNeedProto
(GribFileRecord *therec,GribParamList *step)
#else
(therec,step)
GribFileRecord *therec;
GribParamList *step;
#endif
{
        NclGribCacheList *newlist;
	int nvar_dims;

        if(therec->grib_grid_cache == NULL) {
                therec->grib_grid_cache = NclMalloc(sizeof(NclGribCacheList));
                newlist = NULL;
        } else {
                newlist = therec->grib_grid_cache;
                therec->grib_grid_cache = NclMalloc(sizeof(NclGribCacheList));
        }

        therec->grib_grid_cache->grid_number = step->grid_number;
        therec->grib_grid_cache->has_gds = step->has_gds;
        therec->grib_grid_cache->grid_gds_tbl_index = step->grid_gds_tbl_index;
        therec->grib_grid_cache->n_dims = 3;
        nvar_dims = step->var_info.num_dimensions;
        therec->grib_grid_cache->dimsizes[0] = 2;
        therec->grib_grid_cache->dimsizes[1] = step->var_info.dim_sizes[nvar_dims - 2];
        therec->grib_grid_cache->dimsizes[2] = step->var_info.dim_sizes[nvar_dims - 1];
        therec->grib_grid_cache->dim_ids[0] = step->var_info.file_dim_num[nvar_dims - 3];
        therec->grib_grid_cache->dim_ids[1] = step->var_info.file_dim_num[nvar_dims - 2];
        therec->grib_grid_cache->dim_ids[2] = step->var_info.file_dim_num[nvar_dims - 1];
        therec->grib_grid_cache->int_missing_rec = NULL;
        therec->grib_grid_cache->float_missing_rec = NULL;
        therec->grib_grid_cache->n_entries = 0;
        therec->grib_grid_cache->thelist = NULL;
        therec->grib_grid_cache->next = newlist;
}
static NclMultiDValData  _GetCacheVal
#if     NhlNeedProto
(GribFileRecord *therec,GribParamList *step,GribRecordInqRec *current_rec)
#else
(therec,step,current_rec)
GribFileRecord *therec;
GribParamList *step;
GribRecordInqRec *current_rec;
#endif
{
        NclGribCacheList *thelist;
        NclGribCacheRec *tmp;
        int i;
        int tg;
        void *val;
        int nvar_dims;

        thelist = therec->grib_grid_cache;
        nvar_dims = step->var_info.num_dimensions;
        while(thelist != NULL) {
		/*      
                if((thelist->grid_number == step->grid_number)&&(thelist->has_gds ==step->has_gds)
		   &&(thelist->grid_gds_tbl_index == step->grid_gds_tbl_index)) {
		*/
                if (step->var_info.doff == 2) {
                        if (thelist->n_dims != 3 ||
                            thelist->dim_ids[1] != step->var_info.file_dim_num[nvar_dims-2] ||
                            thelist->dim_ids[2] != step->var_info.file_dim_num[nvar_dims-1]) {
                                thelist = thelist->next;
                                continue;
                        }
                }
                else if (thelist->n_dims != 2 ||
                         thelist->dim_ids[0] != step->var_info.file_dim_num[nvar_dims-2] ||
                         thelist->dim_ids[1] != step->var_info.file_dim_num[nvar_dims-1]) {
			thelist = thelist->next;
			continue;
                }

                if(thelist->n_entries == NCL_GRIB_CACHE_SIZE) {
                        tmp = thelist->tail;
                        tmp->rec->the_dat = NULL;
                        tmp->rec = current_rec;
                        tmp->prev->next = NULL;
                        thelist->tail = tmp->prev;
                        tmp->prev = NULL;
                        tmp->next = thelist->thelist;
                        tmp->next->prev = tmp;
                        thelist->thelist = tmp;
                        return(tmp->thevalue);
                }
                if(thelist->n_entries == 0) {
                        thelist->thelist = NclMalloc(sizeof(NclGribCacheRec));
                        thelist->thelist->prev = NULL;
                        thelist->thelist->next = NULL;
                        thelist->thelist->rec = current_rec;
                        thelist->tail = thelist->thelist;
                        tg = 1;
                        for(i = 0; i< thelist->n_dims; i++) {
                                tg*=thelist->dimsizes[i];
                        }
                        val = NclMalloc(sizeof(float)*tg);
                        thelist->thelist->thevalue = _NclCreateVal(NULL,
                                                                   NULL,
                                                                   Ncl_MultiDValData,
                                                                   0,
                                                                   val,
                                                                   NULL,
                                                                   thelist->n_dims,
                                                                   thelist->dimsizes,
                                                                   PERMANENT,
                                                                   NULL,
                                                                   nclTypefloatClass);
                        thelist->n_entries = 1;
                        return(thelist->thelist->thevalue);
                } else {
                        tmp = NclMalloc(sizeof(NclGribCacheRec));
                        tmp->prev = NULL;
                        tmp->next = thelist->thelist;
                        tmp->next->prev = tmp;
                        tmp->rec = current_rec;
                        tg = 1;
                        for(i = 0; i< thelist->n_dims; i++) {
                                tg*=thelist->dimsizes[i];
                        }
                        val = NclMalloc(sizeof(float)*tg);
                        tmp->thevalue = _NclCreateVal(NULL,
                                                      NULL,
                                                      Ncl_MultiDValData,
                                                      0,
                                                      val,
                                                      NULL,
                                                      thelist->n_dims,
                                                      thelist->dimsizes,
                                                      PERMANENT,
                                                      NULL,
                                                      nclTypefloatClass);
                        ++thelist->n_entries;

                        thelist->thelist = tmp;
                        return(tmp->thevalue);
                }
        }
        return(NULL);
}

static NclMultiDValData  _GetCacheMissingVal
#if     NhlNeedProto
(GribFileRecord *therec,GribParamList *step,GribRecordInqRec *current_rec)
#else
(therec,step,current_rec)
GribFileRecord *therec;
GribParamList *step;
GribRecordInqRec *current_rec;
#endif
{
        NclGribCacheList *thelist;
        int i;
        int tg;
        void *tmp;
        NclScalar missingv;
        int nvar_dims;

        thelist = therec->grib_grid_cache;
        nvar_dims = step->var_info.num_dimensions;
        while(thelist != NULL) {
                if (step->var_info.doff == 2) {
                        if (thelist->n_dims != 3 ||
                            thelist->dim_ids[1] != step->var_info.file_dim_num[nvar_dims-2] ||
                            thelist->dim_ids[2] != step->var_info.file_dim_num[nvar_dims-1]) {
                                thelist = thelist->next;
                                continue;
                        }
                }
                else if (thelist->n_dims != 2 ||
                         thelist->dim_ids[0] != step->var_info.file_dim_num[nvar_dims-2] ||
                         thelist->dim_ids[1] != step->var_info.file_dim_num[nvar_dims-1]) {
			thelist = thelist->next;
			continue;
                }

                if(step->var_info.data_type == NCL_int) {
                        if (thelist->int_missing_rec != NULL) {
                                return thelist->int_missing_rec;
                        }
                        tg = 1;
                        for(i = 0; i <  thelist->n_dims; i++) {
                                tg *= thelist->dimsizes[i];
                        }
                        tmp = NclMalloc(sizeof(int) * tg);
                        for( i = 0; i < tg; i++){
                                ((int*)tmp)[i] = DEFAULT_MISSING_INT;
                        }
                        missingv.intval = DEFAULT_MISSING_INT;

                        thelist->int_missing_rec  = _NclCreateVal(
                                NULL,
                                NULL,
                                Ncl_MultiDValData,
                                0,
                                tmp,
                                &missingv,
                                thelist->n_dims,
                                thelist->dimsizes,
                                PERMANENT,
                                NULL,
                                nclTypeintClass);
                        return thelist->int_missing_rec;
                } else {
                        if (thelist->float_missing_rec != NULL) {
                                return thelist->float_missing_rec;
                        }
                        tg = 1;
                        for(i = 0; i <  thelist->n_dims; i++) {
                                tg *= thelist->dimsizes[i];
                        }
                        tmp = NclMalloc(sizeof(float) * tg);
                        for( i = 0; i < tg; i++){
                                ((float*)tmp)[i] = DEFAULT_MISSING_FLOAT;
                        }
                        missingv.floatval = DEFAULT_MISSING_FLOAT;

                        thelist->float_missing_rec  = _NclCreateVal(
                                NULL,
                                NULL,
                                Ncl_MultiDValData,
                                0,
                                tmp,
                                &missingv,
                                thelist->n_dims,
                                thelist->dimsizes,
                                PERMANENT,
                                NULL,
                                nclTypefloatClass
                                );
                        return thelist->float_missing_rec;
                }
        }
        return(NULL);
}

static NclMultiDValData  _GribGetInternalVar
#if	NhlNeedProto
(GribFileRecord * therec,NclQuark name_q, NclGribFVarRec **vrec)
#else
(therec,name_q)
GribFileRecord * therec;
NclQuark name_q;
#endif
{
	GribInternalVarList *vstep; 

	vstep = therec->internal_var_list;
	while(vstep != NULL ) {
		if(vstep->int_var->var_info.var_name_quark == name_q) {
			*vrec = &vstep->int_var->var_info;
			return(vstep->int_var->value);
		} else {
			vstep = vstep->next;
		}
	}
	*vrec = NULL;
	return(NULL);
}

static void _GribAddInternalVar
#if	NhlNeedProto
(GribFileRecord *therec,NclQuark name_q,int *dim_numbers, NclMultiDValData tmp_md,GribAttInqRecList *attlist,int natts)
#else
(therec,dim_name_q,tmp_md)
GribFileRecord *therec;
NclQuark dim_name_q;
NclMultiDValData *tmp_md;
GribAttInqRecList *attlist;
int natts;
#endif
{
	GribInternalVarList *vstep; 
	GribInternalVarRec *tmp;
	int i;

	tmp = (GribInternalVarRec*)NclMalloc(sizeof(GribInternalVarRec));
	tmp->var_info.data_type = tmp_md->multidval.data_type;
	tmp->var_info.var_name_quark = name_q;
	tmp->var_info.num_dimensions = tmp_md->multidval.n_dims;
	for (i = 0; i < tmp_md->multidval.n_dims; i++) {
		tmp->var_info.file_dim_num[i] = dim_numbers[i];
	}
	tmp->value = tmp_md;
	vstep = (GribInternalVarList*)NclMalloc(sizeof(GribInternalVarList));
	vstep->next = therec->internal_var_list;
	vstep->int_var = tmp;
	vstep->int_var->n_atts = natts;
	vstep->int_var->theatts = attlist;
	therec->internal_var_list = vstep;
	therec->n_internal_vars++;
	return;

}

static void _GribFreeGribRec
#if	NhlNeedProto
(GribRecordInqRec *grib_rec)
#else
(grib_rec)
GribRecordInqRec *grib_rec;
#endif
{
	if(grib_rec->var_name != NULL) {
		NclFree(grib_rec->var_name);
	}
	if(grib_rec->gds != NULL) {
		NclFree(grib_rec->gds);
	}
	if(grib_rec->pds != NULL) {
		NclFree(grib_rec->pds);
	}
	/* if var_name_q is not set then it's a missing record -- shared for all missing records */
	if(grib_rec->the_dat != NULL) {
		if (grib_rec->var_name_q > NrmNULLQUARK)
			_NclDestroyObj((NclObj)grib_rec->the_dat);
	}
	NclFree(grib_rec);
}

static void _GribFreeParamRec
#if	NhlNeedProto
(GribParamList * vstep)
#else
(vstep)
GribParamList * vstep;
#endif
{
	int i;
	GribAttInqRecList *astep= NULL,*tmp =NULL;
	if(vstep != NULL){
		if(vstep->it_vals != NULL) {
			NclFree(vstep->it_vals);
		}
		for(i= 0; i< vstep->n_entries; i++) {
			if(vstep->thelist[i].rec_inq != NULL) {
				_GribFreeGribRec(vstep->thelist[i].rec_inq);
			}
		}
		if(vstep->forecast_time != NULL) {
			_NclDestroyObj((NclObj)vstep->forecast_time);
		}
		if(vstep->yymmddhh!= NULL) {
			_NclDestroyObj((NclObj)vstep->yymmddhh);
		}
		if(vstep->levels!= NULL) {
			_NclDestroyObj((NclObj)vstep->levels);
		}
		if(vstep->levels0!= NULL) {
			_NclDestroyObj((NclObj)vstep->levels0);
		}
		if(vstep->levels1!= NULL) {
			_NclDestroyObj((NclObj)vstep->levels1);
		}
		astep = vstep->theatts;
		for(i = 0; i < vstep->n_atts; i++) {
			_NclDestroyObj((NclObj)astep->att_inq->thevalue);
			NclFree(astep->att_inq);	
			tmp = astep;
			astep = astep->next;
			NclFree(tmp);
		}
		NclFree(vstep->thelist);
		NclFree(vstep);
	}
	return;
}

static NclBasicDataTypes GribMapToNcl 
#if	NhlNeedProto
(void* the_type)
#else
(the_type)
	void *the_type;
#endif
{
	int int_or_float = *(int*)the_type;

	if(int_or_float) {
		return(NCL_int);
	} else {
		return(NCL_float);
	}
}

static void *GribMapFromNcl
#if	NhlNeedProto
(NclBasicDataTypes the_type)
#else
(the_type)
	NclBasicDataTypes the_type;
#endif
{
	int *tmp ;

	tmp = (int*)NclMalloc((unsigned)sizeof(int));
	
	switch(the_type) {
	case NCL_int:
		*tmp = 1;
		break;
	case NCL_float:
		*tmp = 0;
		break;
	default:
		*tmp = -1;
	}
	return((void*)tmp);
}
static int LVNotEqual( GribRecordInqRecList *s_1,GribRecordInqRecList *s_2)
{

	if((s_1->rec_inq->level0 != -1)&&(s_1->rec_inq->level1 != -1)) {
		if(s_1->rec_inq->level0 == s_2->rec_inq->level0) {
			if(s_1->rec_inq->level1 == s_2->rec_inq->level1) {
				return(0);
			} else {
				return(s_1->rec_inq->level1 - s_2->rec_inq->level1);
			}
		} else {
			return(s_1->rec_inq->level0 - s_2->rec_inq->level0);
		}
	} else {
		if(s_1->rec_inq->level0 == s_2->rec_inq->level0) {
			return(0);
		} else {
			return(s_1->rec_inq->level0 - s_2->rec_inq->level0);
		}
	} 
}
static int GetLVList
#if 	NhlNeedProto
(GribFileRecord *therec,
 GribParamList *thevar,GribRecordInqRecList *lstep,int** lv_vals, int** lv_vals1) 
#else
(therec,thevar,lstep,lv_vals, lv_vals1) 
GribFileRecord *therec;
GribParamList *thevar;
GribRecordInqRecList *lstep;
int** lv_vals; 
int** lv_vals1; 
#endif
{
	int n_lvs = 1;
	int i;
	GribRecordInqRecList *strt,*tmp;
	strt = lstep;
/*
	fprintf(stdout,"%d/%d/%d\t(%d:%d)-%d,%d\t%d,%d\ttoff=%d\t%d,%d,%d\n",
		(int)strt->rec_inq->pds[13],
		(int)strt->rec_inq->pds[14],
		(int)strt->rec_inq->pds[12],
		(int)strt->rec_inq->pds[15],
		(int)strt->rec_inq->pds[16],
		(int)strt->rec_inq->pds[18],
		(int)strt->rec_inq->pds[19],
		(int)strt->rec_inq->pds[17],
		(int)strt->rec_inq->pds[20],
		strt->rec_inq->time_offset,
		(int)strt->rec_inq->pds[9],
		strt->rec_inq->level0,
		strt->rec_inq->level1);
*/

	while(strt->next != NULL) {
		if(!LVNotEqual(strt,strt->next)) {
			if ((int)(therec->options[GRIB_PRINT_RECORD_INFO_OPT].values) != 0) {
				NhlPError(NhlWARNING,NhlEUNKNOWN,
					  "NclGRIB: %s contains possibly duplicated records %d and %d. Record %d will be ignored.",
					  NrmQuarkToString(thevar->var_info.var_name_quark),strt->rec_inq->rec_num, 
					  strt->next->rec_inq->rec_num,
					  strt->next->rec_inq->rec_num);
			}
/*
		if((strt->rec_inq->level0 == strt->next->rec_inq->level0)&&(strt->rec_inq->level1 == strt->next->rec_inq->level1)) {
			if((strt->rec_inq->bds_flags & (char)0360) != (strt->next->rec_inq->bds_flags&(char)0360)) {
				fprintf(stdout,"Dup BDSC: Flag error\n");
			}
			if(strt->rec_inq->has_bms != strt->next->rec_inq->has_bms) {
				fprintf(stdout,"Dup BMSC: BMS error\n");
			}
			if(strt->rec_inq->pds_size != strt->next->rec_inq->pds_size) {
				fprintf(stdout,"Dup PDSC: Size Error\n");
			} else {
				fprintf(stdout,"Dup PDSC: %s\t%d\n",(! memcmp(strt->rec_inq->pds,
				strt->next->rec_inq->pds,strt->rec_inq->pds_size)?"Equal":"Not Equal"),(int)strt->rec_inq->pds[9]);
			}
			fprintf(stdout,"GDS SIZE (%d)\n",strt->rec_inq->gds_size);
			if(strt->rec_inq->gds_size != strt->next->rec_inq->gds_size) {
				fprintf(stdout,"Dup GDSC: Size Error\n");
			} else {
				fprintf(stdout,"Dup GDSC: %s\n",(GdsCompare(strt->rec_inq->gds,strt->rec_inq->gds_size,
				strt->next->rec_inq->gds,strt->next->rec_inq->gds_size)?"Equal":"Not Equal"));
			}
			fprintf(stdout,"Dup: (%s,%s)\t(%d,%d)\n",strt->rec_inq->var_name,strt->next->rec_inq->var_name,strt->rec_inq->start,strt->next->rec_inq->start);
*/
/*
			NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Duplicate GRIB record found!! NCL has no way of knowing which is valid, so skipping one arbitrarily!");
			tmp = strt->next;
			strt->next = strt->next->next;
			if(tmp->rec_inq->var_name != NULL) {
				NclFree(tmp->rec_inq->var_name);
			}
*
* Very important to update n_entries.
* Needed later on in _MakeArray
*
			thevar->n_entries--;
			NclFree(tmp->rec_inq);
			NclFree(tmp);
*/
/*
			n_lvs++;
			strt = strt->next;
*/

			tmp = strt->next;
                        strt->next = strt->next->next;
			thevar->n_entries--;
/*
 * dib note 2002-12-13: doesn't free the_dat -- why not??
 */
			if(tmp->rec_inq->var_name != NULL) NclFree(tmp->rec_inq->var_name);
			if(tmp->rec_inq->gds != NULL) NclFree(tmp->rec_inq->gds);
			if(tmp->rec_inq->pds != NULL) NclFree(tmp->rec_inq->pds);
                        NclFree(tmp->rec_inq);
                        NclFree(tmp);

			
		} else {
/*
			fprintf(stdout,"%d/%d/%d\t(%d:%d)-%d,%d\t%d,%d\ttoff=%d\t%d,%d,%d\n",
				(int)strt->next->rec_inq->pds[13],
				(int)strt->next->rec_inq->pds[14],
				(int)strt->next->rec_inq->pds[12],
				(int)strt->next->rec_inq->pds[15],
				(int)strt->next->rec_inq->pds[16],
				(int)strt->next->rec_inq->pds[18],
				(int)strt->next->rec_inq->pds[19],
				(int)strt->next->rec_inq->pds[17],
				(int)strt->next->rec_inq->pds[20],
				strt->next->rec_inq->time_offset,
				(int)strt->next->rec_inq->pds[9],
				strt->next->rec_inq->level0,
				strt->next->rec_inq->level1);
*/

			n_lvs++;
			strt = strt->next;
		}
	}
	strt = lstep;
	*lv_vals = (int*)NclMalloc((unsigned)sizeof(int)*n_lvs);
	if(strt->rec_inq->level1 != -1) {
		*lv_vals1 = (int*)NclMalloc((unsigned)sizeof(int)*n_lvs);
	}
	for(i = 0; i < n_lvs; i++) {
		(*lv_vals)[i] = strt->rec_inq->level0;
		if(strt->rec_inq->level1 != -1) {
			(*lv_vals1)[i] = strt->rec_inq->level1;
		}
		strt = strt->next;
	}
	return(n_lvs);
}

static void Merge2
#if 	NhlNeedProto
(int *tmp_lvs,int *tmp_lvs1,int *tmp_n_lvs,int *lv_vals,int *lv_vals1,int n_lv,int** out_lvs0,int **out_lvs1)
#else
(tmp_lvs,tmp_lvs,tmp_n_lvs,lv_vals,lv_vals1,n_lv,out_lvs0,out_lvs1)
int *tmp_lvs;
int *tmp_lvs1;
int *tmp_n_lvs;
int *lv_vals;
int *lv_vals1;
int n_lv;
int **out_lvs0;
int **out_lvs1;
#endif
{
	int i,j,k;
	int *tmp_out_lvs = NULL;
	int *tmp_out_lvs1 = NULL;

	i = 0;	
	j = 0;
	k = 0;

	tmp_out_lvs = (int*)NclMalloc((unsigned)sizeof(int)*(*tmp_n_lvs + n_lv));
	tmp_out_lvs1 = (int*)NclMalloc((unsigned)sizeof(int)*(*tmp_n_lvs + n_lv));


		
	while((i < *tmp_n_lvs)&&(j< n_lv)) {
		if((tmp_lvs[i] == lv_vals[j])&&(tmp_lvs1[i] == lv_vals1[j])) {
			tmp_out_lvs[k] = tmp_lvs[i];
			tmp_out_lvs1[k] = tmp_lvs1[i];
			i++;
			j++;
			k++;
		} else if((tmp_lvs[i] < lv_vals[j])||((tmp_lvs[i] == lv_vals[j])&&(tmp_lvs1[i] != lv_vals1[j]))){
			tmp_out_lvs[k] = tmp_lvs[i];
			tmp_out_lvs1[k] = tmp_lvs1[i];
			k++;
			i++;
		} else {
			tmp_out_lvs[k] = lv_vals[j];
			tmp_out_lvs1[k] = lv_vals1[j];
			k++;
			j++;
		}
	}
	if(i< *tmp_n_lvs) {
		for( ; i < *tmp_n_lvs;i++) {
			tmp_out_lvs[k] = tmp_lvs[i];
			tmp_out_lvs1[k] = tmp_lvs1[i];
			k++;
		}	
	} else {
		for( ; j < n_lv ;j++) {
			tmp_out_lvs[k] = lv_vals[j];
			tmp_out_lvs1[k] = lv_vals1[j];
			k++;
		}	
	}
	

	NclFree(tmp_lvs);
	NclFree(tmp_lvs1);
	*tmp_n_lvs = k;	
	*out_lvs0 = tmp_out_lvs;
	*out_lvs1 = tmp_out_lvs1;
	return;
}

static int *Merge
#if 	NhlNeedProto
(int *tmp_lvs,int *tmp_n_lvs,int *lv_vals,int n_lv)
#else
(tmp_lvs,tmp_n_lvs,lv_vals,n_lv)
int *tmp_lvs;
int *tmp_n_lvs;
int *lv_vals;
int n_lv;
#endif
{
	int i,j,k;
	int *out_lvs = NULL;

	i = 0;	
	j = 0;
	k = 0;

	out_lvs = (int*)NclMalloc((unsigned)sizeof(int)*(*tmp_n_lvs + n_lv));

		
	while((i < *tmp_n_lvs)&&(j< n_lv)) {
		if(tmp_lvs[i] == lv_vals[j]) {
			out_lvs[k] = tmp_lvs[i];
			i++;
			j++;
			k++;
		} else if(tmp_lvs[i] < lv_vals[j]) {
			out_lvs[k] = tmp_lvs[i];
			k++;
			i++;
		} else {
			out_lvs[k] = lv_vals[j];
			k++;
			j++;
		}
	}
	if(i< *tmp_n_lvs) {
		for( ; i < *tmp_n_lvs;i++) {
			out_lvs[k] = tmp_lvs[i];
			k++;
		}	
	} else {
		for( ; j < n_lv ;j++) {
			out_lvs[k] = lv_vals[j];
			k++;
		}	
	}
	

	NclFree(tmp_lvs);
	*tmp_n_lvs = k;	
	return(out_lvs);
}

int it_comp (GIT *it1,GIT* it2)
{
	int return_val;

	return_val = it1->year - it2->year;

	if (! return_val) {
		return_val = it1->days_from_jan1 - it2->days_from_jan1;
		if (! return_val) {
			return_val = it1->minute_of_day - it2->minute_of_day;
		}
	}
	return (return_val);
}

static GIT *MergeIT
#if 	NhlNeedProto
(GIT *tmp_it_vals,int *tmp_n_it_vals,GIT *it_vals,int n_it)
#else
(tmp_it_vals,tmp_n_it_vals,it_vals,n_it)
GIT *tmp_it_vals;
int *tmp_n_it_vals;
GIT *it_vals;
int n_it;
#endif
{
	int i,j,k;
	GIT *out_it_vals = NULL;

	i = 0;	
	j = 0;
	k = 0;

	out_it_vals = (GIT*)NclMalloc((unsigned)sizeof(GIT)*(*tmp_n_it_vals + n_it));
		
	while((i < *tmp_n_it_vals)&&(j< n_it)) {
		if(! it_comp(&(tmp_it_vals[i]),&(it_vals[j]))) {
			out_it_vals[k] = tmp_it_vals[i];
			i++;
			j++;
			k++;
		} else if(it_comp(&(tmp_it_vals[i]),&(it_vals[j])) < 0) {
			out_it_vals[k] = tmp_it_vals[i];
			k++;
			i++;
		} else {
			out_it_vals[k] = it_vals[j];
			k++;
			j++;
		}
	}
	if(i< *tmp_n_it_vals) {
		for( ; i < *tmp_n_it_vals;i++) {
			out_it_vals[k] = tmp_it_vals[i];
			k++;
		}	
	} else {
		for( ; j < n_it ;j++) {
			out_it_vals[k] = it_vals[j];
			k++;
		}	
	}
	

	NclFree(tmp_it_vals);
	*tmp_n_it_vals = k;	
	return(out_it_vals);
}

static FTLIST *GetFTList
#if 	NhlNeedProto
(GribFileRecord *therec,
 GribParamList *thevar,
 GribRecordInqRecList *step,
 int* n_ft,int **ft_vals,int* total_valid_lv ,int** valid_lv_vals,int** valid_lv_vals1)
#else
(therec,thevar,step, n_ft, ft_vals,total_valid_lv, valid_lv_vals, valid_lv_vals1)
GribFileRecord *therec;
GribParamList *thevar;
GribRecordInqRecList *fstep;
int* n_ft;
int **ft_vals;
int* total_valid_lv;
int** valid_lv_vals;
int** valid_lv_vals1;
#endif
{
	int i;
	GribRecordInqRecList *strt,*fnsh,*fstep;
	int n_fts = 0;
	int current_offset;
	FTLIST header;
	FTLIST *the_end;
	int *tmp_lvs = NULL;
	int *tmp_lvs1 = NULL;
	int tmp_n_lvs = 0;


	the_end = &header;
	the_end->next = NULL;
	
	strt = fstep = step;

	current_offset = strt->rec_inq->time_offset;
	while(fstep->next != NULL) {
		if(fstep->next->rec_inq->time_offset != current_offset) {
			fnsh = fstep;
			fstep = fstep->next;
			fnsh->next = NULL;
			the_end->next = (FTLIST*)NclMalloc((unsigned)sizeof(FTLIST));
			the_end = the_end->next;
			the_end->ft = current_offset;
			the_end->thelist = strt;
			the_end->next = NULL;
			the_end->lv_vals = NULL;
			the_end->lv_vals1 = NULL;
			the_end->n_lv = 0;
			the_end->n_lv = GetLVList(therec,thevar,strt,&the_end->lv_vals,&the_end->lv_vals1);
			if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 == -1)) {
				if(tmp_lvs == NULL) {
					tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
					tmp_n_lvs = the_end->n_lv;
					memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
				} else {
					tmp_lvs = Merge(tmp_lvs,&tmp_n_lvs,the_end->lv_vals,the_end->n_lv);
				}
			} else if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 != -1)){
/*
* Handle multiple value coordinate levels
*/
				if(tmp_lvs == NULL) {
					tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
					tmp_lvs1 = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
					tmp_n_lvs = the_end->n_lv;
					memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
					memcpy((void*)tmp_lvs1,the_end->lv_vals1,the_end->n_lv*sizeof(int));
				} else {
					Merge2(tmp_lvs,tmp_lvs1,&tmp_n_lvs,the_end->lv_vals,the_end->lv_vals1,the_end->n_lv,&tmp_lvs,&tmp_lvs1);
				}
			}
			strt = fstep;
			current_offset = strt->rec_inq->time_offset;
			n_fts++;
		} else {
			fstep = fstep->next;
		}
	}
	the_end->next =(FTLIST*)NclMalloc((unsigned)sizeof(FTLIST));
	the_end = the_end->next;
	the_end->ft = current_offset;
	the_end->thelist = strt;
	the_end->next = NULL;
	the_end->lv_vals = NULL;
	the_end->lv_vals1 = NULL;
	the_end->n_lv = 0;
	the_end->n_lv = GetLVList(therec,thevar,strt,&the_end->lv_vals,&the_end->lv_vals1);
	if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 == -1)){
		if(tmp_lvs != NULL) {
			tmp_lvs = Merge(tmp_lvs,&tmp_n_lvs,the_end->lv_vals,the_end->n_lv);
		} else {
			tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
			tmp_n_lvs = the_end->n_lv;
			memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
		}
	} else if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 != -1)){
/*
* Handle multiple value coordinate levels
*/
		if(tmp_lvs == NULL) {
			tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
			tmp_lvs1 = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
			tmp_n_lvs = the_end->n_lv;
			memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
			memcpy((void*)tmp_lvs1,the_end->lv_vals1,the_end->n_lv*sizeof(int));
		} else {
			Merge2(tmp_lvs,tmp_lvs1,&tmp_n_lvs,the_end->lv_vals,the_end->lv_vals1,the_end->n_lv,&tmp_lvs,&tmp_lvs1);
		}
	}
	n_fts++;
	*n_ft = n_fts;
	*ft_vals = NclMalloc((unsigned)sizeof(int)*n_fts);
	the_end = header.next;
	for (i = 0; i < n_fts; i++) {
		(*ft_vals)[i] = the_end->ft;
		the_end = the_end->next;
	}
	*total_valid_lv = tmp_n_lvs;
	*valid_lv_vals = tmp_lvs;
	*valid_lv_vals1 = tmp_lvs1;
	return(header.next);
}
static NrmQuark GetItQuark
#if NhlNeedProto
(GIT *the_it)
#else
(the_it)
GIT *the_it;
#endif
{
	int y = 0;
	unsigned short mn = 0;
	unsigned short d = 0;
	char buffer[100];

	HeisDiffDate(1,1,the_it->year,the_it->days_from_jan1,&d,&mn,&y);
	if(mn < 10) {
		sprintf(buffer,"0%d/",mn);
	} else {
		sprintf(buffer,"%d/",mn);
	}
	if(d < 10) {
		sprintf(&(buffer[strlen(buffer)]),"0%d/",d);
	} else {
		sprintf(&(buffer[strlen(buffer)]),"%d/",d);
	}
	sprintf(&(buffer[strlen(buffer)]),"%d ",y);

	if(((int)the_it->minute_of_day / 60) < 10) {
		sprintf(&(buffer[strlen(buffer)]),"(0%d:",(int)the_it->minute_of_day / 60);
	} else {
		sprintf(&(buffer[strlen(buffer)]),"(%d:",(int)the_it->minute_of_day / 60);
	}
	if(((int)the_it->minute_of_day % 60) < 10 ) {
		sprintf(&(buffer[strlen(buffer)]),"0%d)",(int)the_it->minute_of_day % 60);
	} else {
		sprintf(&(buffer[strlen(buffer)]),"%d)",(int)the_it->minute_of_day % 60);
	}
	return(NrmStringToQuark(buffer));
}

static NrmQuark GetEnsQuark
#if NhlNeedProto
(ENS *ens)
#else
(ens)
ENS *ens;
#endif
{
	char buf[256];

	if (ens->extension_type == 0) { /* NCEP extension */
		if (ens->type == 5 && ens->prob_type > 0) {
			switch (ens->prob_type) {
			case 1:
				sprintf(buf,"Probability of event below %f",ens->lower_prob * 1e-6);
				break;
			case 2:
				sprintf(buf,"Probability of event above %f",ens->upper_prob * 1e-6);
				break;
			case 3:
				sprintf(buf,"Probability of event between %f and %f",
					ens->lower_prob * 1e-6,ens->upper_prob * 1e-6);
				break;
			}
		}
		else {
			switch (ens->type) {
			case 1:
				sprintf(buf,"%s resolution control forecast",(ens->id == 1 ? "high" : "low"));
				break;
			case 2:
				sprintf(buf,"negative pertubation # %d",ens->id);
				break;
			case 3:
				sprintf(buf,"positive pertubation # %d",ens->id);
				break;
			case 4:
				sprintf(buf,"cluster, prod_id = %d",ens->prod_id);
				break;
			case 5:
				if (ens->n_members > 0) 
					sprintf(buf,"whole ensemble (%d members): prod_id = %d",ens->n_members,ens->prod_id);
				else 
					sprintf(buf,"whole ensemble: prod_id = %d",ens->prod_id);
				break;
			default:
				sprintf(buf,"type: %d, id: %d, prod_id: %d",ens->type,ens->id,ens->prod_id);

			}
		}
	}
	else if (ens->extension_type == 1) {  /* ECMWF local definition 1 */
		switch (ens->type) {
		case 10:
			sprintf(buf,"control forecast");
			break;
		case 11:
			if (ens->id % 2 == 0) 
				sprintf(buf,"negative pertubation # %d",ens->id / 2);
			else 
				sprintf(buf,"positive pertubation # %d",ens->id / 2 + 1);
			break;
		case 17:
			sprintf(buf,"ensemble mean");
			break;
		case 18:
			sprintf(buf,"ensemble standard deviation");
			break;
		default:
			sprintf(buf,"type: %d, id: %d",ens->type,ens->id);
		}

	}
	else {   /* other ECMWF local definitions */
		if (ens->id == 0) 
			sprintf(buf,"control forecast");
		else
			sprintf(buf,"perturbed forecast # %d",ens->id);
			
	}
	return NrmStringToQuark(buf);
}

static NrmQuark ForecastTimeUnitsToQuark
#if 	NhlNeedProto
(int forecast_time_unit)
#else
(forecast_time_unit)
 int forecast_time_unit;
#endif
{
	switch (forecast_time_unit) {
	case 0:
		return (NrmStringToQuark("minutes"));
	case 1:
		return (NrmStringToQuark("hours"));
	case 2:
		return (NrmStringToQuark("days"));
	case 3:
		return (NrmStringToQuark("months"));
	case 4:
		return (NrmStringToQuark("years"));
	case 5:
		return (NrmStringToQuark("decades"));
	case 6:
		return (NrmStringToQuark("normals (30 years)"));
	case 7:
		return (NrmStringToQuark("centuries"));
	case 10:
		return (NrmStringToQuark("3 hours"));
	case 11:
		return (NrmStringToQuark("6 hours"));
	case 12:
		return (NrmStringToQuark("12 hours"));
	case 13:
		return (NrmStringToQuark("15 minutes"));
	case 14:
		return (NrmStringToQuark("30 minutes"));
	case 254:
		return (NrmStringToQuark("seconds"));
	default:
		return (NrmStringToQuark("unknown"));
	}
}

static void _SetAttributeLists
#if 	NhlNeedProto
(GribFileRecord *therec)
#else
(therec)
GribFileRecord *therec;
#endif
{
	GribParamList *step = NULL;
	NclQuark *tmp_string = NULL;
	int *tmp_int = NULL;
	ng_size_t tmp_dimsizes = 1;
	GribRecordInqRec *grib_rec = NULL;
	GribAttInqRecList *att_list_ptr= NULL;
	int i;
	int *tmp_level = NULL;
	void *tmp_fill = NULL;
	char buffer[512];


	step = therec->var_list;
	
	while(step != NULL) {
/*
* Handle long_name, units, center, sub_center, model and _FillValue
*/	
		for(i = 0; i < step->n_entries; i++) {
			if(step->thelist[i].rec_inq != NULL) {
				grib_rec = step->thelist[i].rec_inq;
				break;
			}
		}


/*
* Handle coordinate attributes,  ensemble, level, initial_time, forecast_time
*/
		if(step->ensemble_isatt && step->prob_param) {
			float *tmp_probs;
			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			switch (grib_rec->ens.prob_type) {
			case 1:
			case 2:
				att_list_ptr->att_inq->name = NrmStringToQuark("probability_limit");
				att_list_ptr->att_inq->thevalue = (NclMultiDValData)step->probability;
				step->probability = NULL;
				step->theatts = att_list_ptr;
				step->n_atts++;
				break;
			case 3:
				tmp_probs = (float*)NclMalloc(sizeof(float) * 2);
				att_list_ptr->att_inq->name = NrmStringToQuark("probability_limits");
				tmp_probs[0] = *(int*)step->lower_probs->multidval.val;
				tmp_probs[1] = *(int*)step->upper_probs->multidval.val;
				tmp_dimsizes = 2;
				att_list_ptr->att_inq->thevalue = (NclMultiDValData)
					_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, 
						       (void*)tmp_probs, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypeintClass);
				tmp_dimsizes = 1;
				_NclDestroyObj((NclObj)step->lower_probs);
				_NclDestroyObj((NclObj)step->upper_probs);
				step->lower_probs = step->upper_probs = NULL;
				step->theatts = att_list_ptr;
				step->n_atts++;
				break;
			}
		}
		else if (grib_rec->is_ensemble && step->ensemble_isatt) {
			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("ensemble_info");
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)step->ensemble;
			step->ensemble = NULL;

			_NclDestroyObj((NclObj)step->ens_indexes);
			step->ens_indexes = NULL;
			step->theatts = att_list_ptr;
			step->n_atts++;
		}
		if (step->prob_param) {
			att_list_ptr = (GribAttInqRecList *) NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec *) NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("probability_type");
			tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
			switch (grib_rec->ens.prob_type) {
			case 1:
				*tmp_string = NrmStringToQuark("Probability of event below limit");
				break;
			case 2:
				*tmp_string = NrmStringToQuark("Probability of event above limit");
				break;
			case 3:
				*tmp_string = NrmStringToQuark("Probability of event between upper and lower limits");
				break;
			default:
				*tmp_string = NrmStringToQuark("Unknown");
			}
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)
				_NclCreateVal(NULL, NULL,
					      Ncl_MultiDValData, 0, (void *) tmp_string, NULL, 1, &tmp_dimsizes, 
					      PERMANENT, NULL, nclTypestringClass);
			step->theatts = att_list_ptr;
			step->n_atts++;
		}

		if(step->yymmddhh_isatt) {
			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("initial_time");
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)step->yymmddhh;
/*
* Don't want two references
*/
			step->yymmddhh = NULL;
			step->theatts = att_list_ptr;
			step->n_atts++;
		}
		if(step->forecast_time_isatt) {
			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("forecast_time_units");
			tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*tmp_string = ForecastTimeUnitsToQuark(step->time_unit_indicator);
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)
				_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, 
					       (void*)tmp_string, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
			step->theatts = att_list_ptr;
			step->n_atts++;

			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("forecast_time");
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)step->forecast_time;
/*
* Don't want two references
*/
			step->forecast_time= NULL;
			step->theatts = att_list_ptr;
			step->n_atts++;
		}
		if((step->levels_isatt)&&(!step->levels_has_two)) {
			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("level");
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)step->levels;
/*
* Don't want two references
*/
			step->levels= NULL;
			step->theatts = att_list_ptr;
			step->n_atts++;
		} else if((step->levels_isatt)&&(step->levels_has_two)){
/*
* TEMPORARY would like to change attribute name to reflect level indicator
*/
			tmp_level = (int*)NclMalloc(sizeof(int) * 2);
			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("level");

			tmp_level[0] = *(int*)step->levels0->multidval.val;
			tmp_level[1] = *(int*)step->levels1->multidval.val;
			tmp_dimsizes = 2;
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_level, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypeintClass);
			tmp_dimsizes = 1;
/*
* Don't want two references
*/
			_NclDestroyObj((NclObj)step->levels0);
			_NclDestroyObj((NclObj)step->levels1);
			step->levels0= NULL;
			step->levels1= NULL;
			step->theatts = att_list_ptr;
			step->n_atts++;
		}
/*
* model
*/
		switch((int)grib_rec->pds[4]) {
		case 7:
			for( i = 0; i < sizeof(models)/sizeof(GribTable);i++) {
				if(models[i].index == (int)grib_rec->pds[5]) {
					att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
					att_list_ptr->next = step->theatts;
					att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
					att_list_ptr->att_inq->name = NrmStringToQuark("model");
					tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
					*tmp_string = NrmStringToQuark(models[i].name);		
					att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_string, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
					step->theatts = att_list_ptr;
					step->n_atts++;
				}
			}
			break;
		default:
			break;
		}
		att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
		att_list_ptr->next = step->theatts;
		att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
		att_list_ptr->att_inq->name = NrmStringToQuark("parameter_number");
		tmp_int = (int*)NclMalloc(sizeof(int));
		*tmp_int= grib_rec->param_number;
		att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_int, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypeintClass);
		step->theatts = att_list_ptr;
		step->n_atts++;

		att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
		att_list_ptr->next = step->theatts;
		att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
		att_list_ptr->att_inq->name = NrmStringToQuark("parameter_table_version");
		tmp_int = (int*)NclMalloc(sizeof(int));
		*tmp_int= (int)grib_rec->pds[3];
		att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_int, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypeintClass);
		step->theatts = att_list_ptr;
		step->n_atts++;

		att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
		att_list_ptr->next = step->theatts;
		att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
		tmp_int = (int*)NclMalloc(sizeof(int));
		if((step->has_gds)&&(step->grid_number == 255 || step->grid_number == 0 ||  step->grid_tbl_index == -1)) {
			att_list_ptr->att_inq->name = NrmStringToQuark("gds_grid_type");
			*tmp_int = grib_rec->gds_type;
		}
		else {
			att_list_ptr->att_inq->name = NrmStringToQuark("grid_number");
			*tmp_int = grib_rec->grid_number;
		}
		att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_int, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypeintClass);
		step->theatts = att_list_ptr;
		step->n_atts++;

		att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
		att_list_ptr->next = step->theatts;
		att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
		att_list_ptr->att_inq->name = NrmStringToQuark("level_indicator");
		tmp_int= (int*)NclMalloc(sizeof(int));
		*tmp_int= grib_rec->level_indicator;
		att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_int, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypeintClass);
		step->theatts = att_list_ptr;
		step->n_atts++;

/*
 * if 2D coordinates, this adds the CF compliant attribute "coordinates", to point to the
 * auxiliary coordinate variables
 */

		if (step->aux_coords[0] != NrmNULLQUARK) {

			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("coordinates");
			tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
			sprintf(buffer,"%s %s",NrmQuarkToString(step->aux_coords[0]),
				NrmQuarkToString(step->aux_coords[1]));
			*tmp_string = NrmStringToQuark(buffer);		
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_string, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
			step->theatts = att_list_ptr;
			step->n_atts++;
		}

		att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
		att_list_ptr->next = step->theatts;
		att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
		att_list_ptr->att_inq->name = NrmStringToQuark(NCL_MISSING_VALUE_ATT);
		if(step->var_info.data_type == NCL_int) {
			tmp_fill = NclMalloc(sizeof(int));
			*(int*)tmp_fill = DEFAULT_MISSING_INT;
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_fill, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypeintClass);
		} else {
			tmp_fill = NclMalloc(sizeof(float));
			*(float*)tmp_fill = DEFAULT_MISSING_FLOAT;
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_fill, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypefloatClass);
		}

		step->theatts = att_list_ptr;
		step->n_atts++;

#if 0
/*
 * whole_ensemble_product
 */
		if (step->prob_ix == 191 || step->prob_ix == 192) {
			att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
			att_list_ptr->next = step->theatts;
			att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
			att_list_ptr->att_inq->name = NrmStringToQuark("whole_ensemble_product_type");
			tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
			if (step->prob_ix == 191)
				*tmp_string = NrmStringToQuark("probability from ensemble");
			else 
				*tmp_string = NrmStringToQuark("probability from ensemble normalized with respect to climate expectancy");
			att_list_ptr->att_inq->thevalue = (NclMultiDValData)
				_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_string, NULL, 
					       1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
			step->theatts = att_list_ptr;
			step->n_atts++;
		}
#endif
		
/*
* units
*/
		att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
		att_list_ptr->next = step->theatts;
		att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
		att_list_ptr->att_inq->name = NrmStringToQuark("units");
		tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*tmp_string = step->var_info.units_q;
		att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_string, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
		step->theatts = att_list_ptr;
		step->n_atts++;
/*
 * long_name
 */
		att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
		att_list_ptr->next = step->theatts;
		att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
		att_list_ptr->att_inq->name = NrmStringToQuark("long_name");
		tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
		if (! step->prob_param) {
			*tmp_string = step->var_info.long_name_q;
		}
		else {
			sprintf(buffer,"%s (%s)",step->prob_param->long_name,NrmQuarkToString(step->var_info.long_name_q));
			*tmp_string = NrmStringToQuark(buffer);
		}
			
		att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_string, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
		step->theatts = att_list_ptr;
		step->n_atts++;



/*
* center
*/
		for( i = 0; i < sizeof(centers)/sizeof(GribTable);i++) {
			if(centers[i].index == (int)grib_rec->pds[4]) {
				att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
				att_list_ptr->next = step->theatts;
				att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
				att_list_ptr->att_inq->name = NrmStringToQuark("center");
				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				*tmp_string = NrmStringToQuark(centers[i].name);		
				att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_string, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
				step->theatts = att_list_ptr;
				step->n_atts++;
			}
		}
/*
*  sub_center
*/
		for( i = 0; i < sizeof(sub_centers)/sizeof(GribTable);i++) {
			if(sub_centers[i].index == (int)grib_rec->pds[25]) {
				att_list_ptr = (GribAttInqRecList*)NclMalloc((unsigned)sizeof(GribAttInqRecList));
				att_list_ptr->next = step->theatts;
				att_list_ptr->att_inq = (GribAttInqRec*)NclMalloc((unsigned)sizeof(GribAttInqRec));
				att_list_ptr->att_inq->name = NrmStringToQuark("sub_center");
				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				*tmp_string = NrmStringToQuark(sub_centers[i].name);		
				att_list_ptr->att_inq->thevalue = (NclMultiDValData)_NclCreateVal( NULL, NULL, Ncl_MultiDValData, 0, (void*)tmp_string, NULL, 1 , &tmp_dimsizes, PERMANENT, NULL, nclTypestringClass);
				step->theatts = att_list_ptr;
				step->n_atts++;
			}
		}

		step = step->next;
	}
}

static void _MakeVarnamesUnique
#if 	NhlNeedProto
(GribFileRecord *therec)
#else
(therec)
GribFileRecord *therec;
#endif
{
	GribParamList *step = NULL;
	char buffer[80];

	for (step = therec->var_list; step != NULL; step = step->next) {
		NclQuark qvname = step->var_info.var_name_quark;
		int nfound = 0;
		GribParamList *tstep = step->next;
		
		for (tstep = step->next; tstep != NULL; tstep = tstep->next) {
			int i;

			if (tstep->var_info.var_name_quark != qvname)
				continue;
			nfound++;
			sprintf(buffer,"%s_%d",NrmQuarkToString(qvname),nfound);
			tstep->var_info.var_name_quark = NrmStringToQuark(buffer);

			for (i = 0; i < tstep->n_entries; i++) {
				GribRecordInqRec *rec = tstep->thelist[i].rec_inq;
				NclFree(rec->var_name);
				rec->var_name = (char*)NclMalloc((unsigned)strlen((char*)buffer) + 1);
				strcpy(rec->var_name,(char*)buffer);
				rec->var_name_q = tstep->var_info.var_name_quark;
			}
		}
	}
}

static void _PrintRecordInfo
#if 	NhlNeedProto
(GribFileRecord *therec)
#else
(therec)
GribFileRecord *therec;
#endif
{
	GribParamList *step = NULL;
	GribRecordInqRecList *tstep;
	int i,j;

	for (step = therec->var_list; step != NULL; step = step->next) {
		NclQuark qvname = step->var_info.var_name_quark;
		int cur_ix[5] = { 0,0,0,0,0};
		int n_dims = step->var_info.doff == 1 ? 
			step->var_info.num_dimensions - 2 : step->var_info.num_dimensions - 3;

		if (n_dims == 0) {
			printf("%s",NrmQuarkToString(qvname));
			tstep = &step->thelist[0];
			printf("%s \t",step->var_info.doff == 1 ? "(:,:)" : "(:,:,:)");
			if (tstep->rec_inq)
				printf("%d\n",tstep->rec_inq->rec_num);
			else
				printf("missing record\n");
			continue;
		}
		for (i = 0; i < step->n_entries; i++) {
			printf("%s",NrmQuarkToString(qvname));
			printf("(");
			for (j = 0; j < n_dims; j++) {
				printf("%d,",cur_ix[j]);
			}
			printf("%s) \t",step->var_info.doff == 1 ? ":,:" : ":,:,:");
			cur_ix[n_dims-1]++;
			tstep = &step->thelist[i];
			if (tstep->rec_inq)
				printf("%d\n",tstep->rec_inq->rec_num);
			else
				printf("missing record\n");
			for (j = n_dims -1; j > 0; j--) {
				if (cur_ix[j] == step->var_info.dim_sizes[j]) {
					cur_ix[j-1]++;
					cur_ix[j] = 0;
				}
			}
		}
	}
}


int GdsCompare(unsigned char *gds1,int gds_size1,unsigned char *gds2,int gds_size2) {
	int i;
	/* 
	 * differences in octet 17 are not currently used by NCL so for now
	 * ignore them. Otherwise multiple seemingly identical coordinates are
	 * created, of which only one can be accessed. This will probably need to be
	 * revisited. dib 2002-11-22
	 */

	/*
	 * Another issue: grids that have levels (gds[3] > 0) look different from ones that don't (or have a 
	 * different number) So skip over the levels when determining whether a GDS is the same.
	 * dib 2004-08-16
	 */

	if (gds_size1 == gds_size2) {
		for( i = 0; i < gds_size1 ; i++) {
			switch (i) {
			case 16:
				break;
			default:
				if(gds1[i] != gds2[i]) {	
					return(0);
				}
				break;
			}
		}
		return(1);
	}
	if (gds1[3] > 0 || gds2[3] > 0) {
		int top1,top2;
		if ((gds_size1 - (int)gds1[3] * 4  != gds_size2 - (int)gds2[3] * 4) ||
		    (gds1[4] != gds2[4])) 
			return 0;
		for( i = 4; i < (int)gds1[4] - 1 ; i++) {
			switch (i) {
			case 16:
				break;
			default:
				if(gds1[i] != gds2[i]) {	
					return(0);
				}
				break;
			}
		}
		top1 = gds1[4] + gds1[3] * 4;
		top2 = gds2[4] + gds2[3] * 4;
		if (top1 < gds_size1) {
			unsigned char *t1 = &gds1[top1-1];
			unsigned char *t2 = &gds2[top2-1];
			for (i = top1 - 1; i < gds_size1; i++)
				if(*(t1++) != *(t2++)) {	
					return(0);
				}
		}
		return(1);
	}
	return 0;
}

float bytes2float (unsigned char *bytes)
{
	float sign;
	float a,b;
	float val;

	sign = (bytes[0] & (char) 0200) ? -1 : 1;
	a = (float) (bytes[0] & (char) 0177);
	b = (float) CnvtToDecimal(3,&(bytes[1]));
	val = sign * b * pow(2.0, -24.0) * pow(16.0, (double)(a - 64));
	return val;
}
					

void _Do109(GribFileRecord *therec,GribParamList *step) {
	ng_size_t dimsizes_level;
	int tmp_file_dim_number;
	int i;
	char buffer[256];
	NclGribFVarRec *test;
	int ok = 0;
	NclMultiDValData tmp_md;
	float *af, *afi = NULL;
	float *bf, *bfi = NULL;
	float *tmpf;
	int nv;
	int pl;
	int the_start_off;
	int sign;
	float tmpb;
	float tmpa;
	ng_size_t count;
	int interface = 0;
	GribAttInqRecList *att_list_ptr= NULL;	
	int attcount;
	GribDimInqRec *tdim = NULL;
	GribDimInqRecList *dimptr;
	int new_dim_number;
	NrmQuark *qstr = NULL;
	NrmQuark qldim;
	int ix;
	GribInternalVarList *ivar;
	int is_dwd = 0;

	for(i = 0; i < step->var_info.num_dimensions; i++) {
		sprintf(buffer,"lv_HYBL%d",step->var_info.file_dim_num[i]);
		if((tmp_md = _GribGetInternalVar(therec,NrmStringToQuark(buffer),&test)) != NULL) {
			dimsizes_level = step->var_info.dim_sizes[i];
			tmp_file_dim_number = step->var_info.file_dim_num[i];
			ok = 1;
			qldim = NrmStringToQuark(buffer);
			break;
		}
	}
	if (! ok)
		return;

	nv = (int)step->thelist->rec_inq->gds[3];
	if(nv == 0) {
		return;
	}
	ix = step->thelist->rec_inq->center_ix;
	sprintf(buffer,"lv_HYBL%d_a",tmp_file_dim_number);
	if ((centers[ix].index == 98 || step->thelist->rec_inq->eff_center == 98) && 
	    (nv / 2 > dimsizes_level)) {
		/* 
		 * ECMWF data - we know that ERA 40 and ERA 15 use level interfaces for A and B 
		 * coefficients in the GRIB file data. And from looking at the ECMWF web site, 
		 * it looks as if hybrid coordinates are always specified using the interface levels.
		 * Time will tell...
		 */

		interface = 1;

		/*
		 * Since a and b are defined on the interfaces generate the complete set of 
		 * interface parameters, plus generate "full" model level a and b values 
		 * by averaging the interface values above and below.
		 */
	}
	else if (centers[ix].index == 78 && nv >= dimsizes_level + 4) {
		is_dwd = 1;
		sprintf(buffer,"lv_HYBL%d_vc",tmp_file_dim_number);
	}

	if((_GribGetInternalVar(therec,NrmStringToQuark(buffer),&test) ==NULL)) {
		pl = (int)step->thelist->rec_inq->gds[4];
		tmpf = (float*)NclMalloc(nv*sizeof(float));
		the_start_off = 4*nv+(pl-1);
		for(i = pl-1;i< the_start_off; i+=4) {
			sign  = (step->thelist->rec_inq->gds[i] & (char) 0200)? 1 : 0;
			tmpa = (float)(step->thelist->rec_inq->gds[i] & (char)0177);
			tmpb = (float)CnvtToDecimal(3,&(step->thelist->rec_inq->gds[i+1]));
			tmpf[(i-(pl-1))/4] = tmpb;
			tmpf[(i-(pl-1))/4] *= (float)pow(2.0,-24.0);
			tmpf[(i-(pl-1))/4] *= (float)pow(16.0,(double)(tmpa - 64));
			if(sign) {
				tmpf[(i-(pl-1))/4] = -tmpf[(i-(pl-1))/4];
			}
		}
		if (is_dwd) {
			float *tf;
			af = (float*)NclMalloc(sizeof(float)*dimsizes_level);
			for(i =0; i < dimsizes_level; i++) {
				int ix = ((int*)tmp_md->multidval.val)[i] + 3;
				if (ix >= 0 && ix < dimsizes_level + 4)
					af[i] = tmpf[ix];
				else
					af[i] = DEFAULT_MISSING_FLOAT;
			}
			/* first four elements are special parameters */
			attcount = 0;
			att_list_ptr = NULL;
			tf = (float*)NclMalloc(sizeof(float));
			*tf = tmpf[0];
			GribPushAtt(&att_list_ptr,"p0s1",tf,1,nclTypefloatClass); 
			attcount++;
			tf = (float*)NclMalloc(sizeof(float));
			*tf = tmpf[1];
			GribPushAtt(&att_list_ptr,"t0s1",tf,1,nclTypefloatClass); 
			attcount++;
			tf = (float*)NclMalloc(sizeof(float));
			*tf = tmpf[2];
			GribPushAtt(&att_list_ptr,"dt01p",tf,1,nclTypefloatClass); 
			attcount++;
			tf = (float*)NclMalloc(sizeof(float));
			*tf = tmpf[3];
			GribPushAtt(&att_list_ptr,"vcflat",tf,1,nclTypefloatClass); 
			attcount++;
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark("vertical coordinate");
			GribPushAtt(&att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
			attcount++;
			_GribAddInternalVar(therec,NrmStringToQuark(buffer),
					    &tmp_file_dim_number,(NclMultiDValData)_NclCreateVal(
						    NULL,
						    NULL,
						    Ncl_MultiDValData,
						    0,
						    (void*)af,
						    NULL,
						    1,
						    &dimsizes_level,
						    TEMPORARY,
						    NULL,
						    nclTypefloatClass),att_list_ptr,attcount);
			NclFree(tmpf);
			return;
		}
		else if (interface) {
			/* 
			 * interface levels are specified -- we need a new dimension,
			 * this depends on _Do109 being called after the regular level dimension 
			 * has been created, but before any others are created. It assumes that the
			 * next dimension number is available.
			 */
			new_dim_number = tmp_file_dim_number + 1;
			count = MIN(dimsizes_level +1,nv / 2);
			tdim = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
			tdim->dim_number = new_dim_number;
			tdim->sub_type_id = 109;
			tdim->is_gds = -1;
			tdim->size = count;
			sprintf(buffer,"lv_HYBL_i%d",new_dim_number);
			tdim->dim_name = NrmStringToQuark(buffer);
			therec->total_dims++;
			dimptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
			dimptr->dim_inq = tdim;
			dimptr->next = therec->lv_dims;
			therec->lv_dims = dimptr;
			therec->n_lv_dims++;

			af = (float*)NclMalloc(sizeof(float)*dimsizes_level);
			bf = (float*)NclMalloc(sizeof(float)*dimsizes_level);
			afi = (float*)NclMalloc(sizeof(float)*count);
			bfi = (float*)NclMalloc(sizeof(float)*count);
			for(i = 0; i < count; i++) {
				int ix;
				if (i < dimsizes_level)
					ix = ((int*)tmp_md->multidval.val)[i] - 1;
				else 
					ix = i;
				afi[i] = tmpf[ix]/100000.0;
				bfi[i] = tmpf[nv/2+ ix];
			}
			for(i =0; i < dimsizes_level; i++) {
				int ix = ((int*)tmp_md->multidval.val)[i];
				if (ix < 1)
					ix = 1;
				if (ix > count - 1)
					ix = count - 1;
				af[i] = 0.5 * (afi[ix - 1] + afi[ix]);
				bf[i] = 0.5 * (bfi[ix - 1] + bfi[ix]);
			}
		}
		else {
			/* level centers are specified */
			af = (float*)NclMalloc(sizeof(float)*dimsizes_level);
			bf = (float*)NclMalloc(sizeof(float)*dimsizes_level);
			for(i =0; i < dimsizes_level; i++) {
				if (((int*)tmp_md->multidval.val)[i] < dimsizes_level)
					af[i] = tmpf[((int*)tmp_md->multidval.val)[i] ]/100000.0;
				else 
					af[i] = DEFAULT_MISSING_FLOAT;
				if (nv/2+((int*)tmp_md->multidval.val)[i] < dimsizes_level)
					bf[i] = tmpf[nv/2+((int*)tmp_md->multidval.val)[i]];
				else 
					bf[i] = DEFAULT_MISSING_FLOAT;
			}
		}
		NclFree(tmpf);

		att_list_ptr = NULL;
		attcount = 0;
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*qstr = NrmStringToQuark("hybrid A coefficient at layer midpoints");
		GribPushAtt(&att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
		attcount++;
		if (interface) {
			sprintf(buffer,"derived from %s_a as average of layer interfaces above and below midpoints",
				NrmQuarkToString(tdim->dim_name));
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark(buffer);
			GribPushAtt(&att_list_ptr,"note",qstr,1,nclTypestringClass); 
			attcount++;
		}

		sprintf(buffer,"lv_HYBL%d_a",tmp_file_dim_number);
		_GribAddInternalVar(therec,NrmStringToQuark(buffer),&tmp_file_dim_number,(NclMultiDValData)_NclCreateVal(
			NULL,
			NULL,
			Ncl_MultiDValData,
			0,
			(void*)af,
			NULL,
			1,
			&dimsizes_level,
			TEMPORARY,
			NULL,
			nclTypefloatClass),att_list_ptr,attcount);

		att_list_ptr = NULL;
		attcount = 0;
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*qstr = NrmStringToQuark("hybrid B coefficient at layer midpoints");
		GribPushAtt(&att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
		attcount++;
		if (interface) {
			sprintf(buffer,"derived from %s_b as average of layer interfaces above and below midpoints",
				NrmQuarkToString(tdim->dim_name));
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark(buffer);
			GribPushAtt(&att_list_ptr,"note",qstr,1,nclTypestringClass); 
			attcount++;
		}
		sprintf(buffer,"lv_HYBL%d_b",tmp_file_dim_number);
		_GribAddInternalVar(therec,NrmStringToQuark(buffer),&tmp_file_dim_number,(NclMultiDValData)_NclCreateVal(
			NULL,
			NULL,
			Ncl_MultiDValData,
			0,
			(void*)bf,
			NULL,
			1,
			&dimsizes_level,
			TEMPORARY,
			NULL,
			nclTypefloatClass),att_list_ptr,attcount);
		
		if (interface) {
			att_list_ptr = NULL;
			attcount = 0;
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark("hybrid A coefficient at layer interfaces");
			GribPushAtt(&att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
			attcount++;
			sprintf(buffer,"layer interfaces associated with hybrid levels lv_HYBL%d",
				tmp_file_dim_number);
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark(buffer);
			GribPushAtt(&att_list_ptr,"note",qstr,1,nclTypestringClass); 
			attcount++;
			sprintf(buffer,"lv_HYBL_i%d_a",new_dim_number);
			_GribAddInternalVar(therec,NrmStringToQuark(buffer),&new_dim_number,(NclMultiDValData)_NclCreateVal(
						    NULL,
						    NULL,
						    Ncl_MultiDValData,
						    0,
						    (void*)afi,
						    NULL,
						    1,
						    &count,
						    TEMPORARY,
						    NULL,
						    nclTypefloatClass),att_list_ptr,attcount);

			att_list_ptr = NULL;
			attcount = 0;
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark("hybrid B coefficient at layer interfaces");
			GribPushAtt(&att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
			attcount++;
			sprintf(buffer,"layer interfaces associated with hybrid levels lv_HYBL%d",
				tmp_file_dim_number);
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark(buffer);
			GribPushAtt(&att_list_ptr,"note",qstr,1,nclTypestringClass); 
			attcount++;
			sprintf(buffer,"lv_HYBL_i%d_b",new_dim_number);
			_GribAddInternalVar(therec,NrmStringToQuark(buffer),&new_dim_number,(NclMultiDValData)_NclCreateVal(
						    NULL,
						    NULL,
						    Ncl_MultiDValData,
						    0,
						    (void*)bfi,
						    NULL,
						    1,
						    &count,
						    TEMPORARY,
						    NULL,
						    nclTypefloatClass),att_list_ptr,attcount);
		}
		/* 
		 * Now we need the scalar reference pressure, requires a scalar dim 
		 * Since this is a constant value only one is needed
		 */
		if (therec->n_scalar_dims == 0) {
			tdim = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
			tdim->dim_number = therec->total_dims;
			tdim->is_gds = -1;
			tdim->size = 1;
			tdim->sub_type_id = 109;
			sprintf(buffer,"ncl_scalar");
			tdim->dim_name = NrmStringToQuark(buffer);
			therec->total_dims++;
			dimptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
			dimptr->dim_inq = tdim;
			dimptr->next = therec->scalar_dims;
			therec->scalar_dims = dimptr;
			therec->n_scalar_dims++;
			att_list_ptr = NULL;
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark("reference pressure");
			GribPushAtt(&att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
			qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
			*qstr = NrmStringToQuark("Pa");
			GribPushAtt(&att_list_ptr,"units",qstr,1,nclTypestringClass); 
			sprintf(buffer,"P0");
			tmpf = (float*)NclMalloc(1*sizeof(float));
			*tmpf = 100000.0;
			count = tdim->size;
			_GribAddInternalVar(therec,NrmStringToQuark(buffer),&tdim->dim_number,(NclMultiDValData)_NclCreateVal(
						    NULL,
						    NULL,
						    Ncl_MultiDValData,
						    0,
						    (void*)tmpf,
						    NULL,
						    1,
						    &count,
						    TEMPORARY,
						    NULL,
						    nclTypefloatClass),att_list_ptr,2);
		}

		/*
		 * Now add attributes to the attribute list of the level coordinate variable
		 */
	
		att_list_ptr = NULL;
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*qstr = NrmStringToQuark("atmosphere hybrid sigma pressure coordinate");
		GribPushAtt(&att_list_ptr,"standard_name",qstr,1,nclTypestringClass); 
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		sprintf(buffer,"a: lv_HYBL%d_a b: lv_HYBL%d_b ps: unknown p0: P0",tmp_file_dim_number,tmp_file_dim_number);
		*qstr = NrmStringToQuark(buffer);
		GribPushAtt(&att_list_ptr,"formula_terms",qstr,1,nclTypestringClass); 
		for (ivar = therec->internal_var_list; ivar != NULL; ivar = ivar->next) {
			if (ivar->int_var->var_info.var_name_quark != qldim) 
				continue;
			att_list_ptr->next->next = ivar->int_var->theatts;
			ivar->int_var->theatts = att_list_ptr;
			ivar->int_var->n_atts += 2;
		}		
	} 
}


static double  *_DateStringsToEncodedDoubles
#if 	NhlNeedProto
(
NrmQuark *vals,
int dimsize
	)
#else
(vals,dimsize)
NrmQuark *vals;
int dimsize;
#endif
{
	int i;
	char *str;
	double *ddates;

	ddates = NclMalloc(dimsize * sizeof(double));
	if (!ddates) {
		NHLPERROR((NhlFATAL,ENOMEM,NULL));
		return NULL;
	}

	for (i = 0; i < dimsize; i++) {
		int y,m,d,h;
		float min;
		str = NrmQuarkToString(vals[i]);
		sscanf(str,"%2d/%2d/%4d (%2d:%2f)",&m,&d,&y,&h,&min);
		ddates[i] = y * 1e6 + m * 1e4 + d * 1e2 + h + min / 60.;
	}
				       
	return ddates;
}

static double  *_DateStringsToHours
#if 	NhlNeedProto
(
NrmQuark *vals,
int dimsize
	)
#else
(vals,dimsize)
NrmQuark *vals;
int dimsize;
#endif
{
	int i;
	char *str;
	double *dhours;

	dhours = NclMalloc(dimsize * sizeof(double));
	if (!dhours) {
		NHLPERROR((NhlFATAL,ENOMEM,NULL));
		return NULL;
	}

	for (i = 0; i < dimsize; i++) {
		int y,m,d,h,min;
		long jddiff;
		str = NrmQuarkToString(vals[i]);
		sscanf(str,"%2d/%2d/%4d (%2d:%2d)",&m,&d,&y,&h,&min);
		jddiff = HeisDayDiff(1,1,1800,d,m,y);
		dhours[i] = jddiff * 24 + h + min / 60.0;
	}
				       
	return dhours;
}



static void SetInitialTimeCoordinates
#if 	NhlNeedProto
(GribFileRecord *therec)
#else
(therec)
GribFileRecord *therec;
#endif
{
	GribDimInqRecList *step;
	GribInternalVarList *vstep,*nvstep;
	int i,j,k;

	step = therec->it_dims;
	for (i = 0; i < therec->n_it_dims; i++) {
		char buffer[128];
		char *cp;
		NrmQuark dimq,newdimq;
			

		dimq = step->dim_inq->dim_name;
		vstep = therec->internal_var_list;
		for (j = 0; j < therec->n_internal_vars; j++) {
			if (vstep->int_var->var_info.var_name_quark == dimq) {
				break;
			}
			vstep = vstep->next;
		}
		if (j == therec->n_internal_vars) {
			printf("var %s not found\n",NrmQuarkToString(dimq));
			continue;
		}
		cp = strrchr(NrmQuarkToString(dimq),'_');
		if (cp && ! strcmp(cp,"_hours")) {
			if ((NrmQuark)therec->options[GRIB_INITIAL_TIME_COORDINATE_TYPE_OPT].values == NrmStringToQuark("numeric"))
				continue;
			sprintf(buffer,NrmQuarkToString(dimq));
			cp = strrchr(buffer,'_');
			*cp = '\0';
			newdimq = NrmStringToQuark(buffer);
			nvstep = therec->internal_var_list;
			for (k = 0; k < therec->n_internal_vars; k++) {
				if (nvstep->int_var->var_info.var_name_quark == newdimq) {
					break;
				}
				nvstep = nvstep->next;
			}
			if (k == therec->n_internal_vars) {
				printf("var %s not found\n",NrmQuarkToString(newdimq));
				continue;
			}
			step->dim_inq->dim_name = newdimq;
		}
		else {
			if ((NrmQuark)therec->options[GRIB_INITIAL_TIME_COORDINATE_TYPE_OPT].values == NrmStringToQuark("string"))
				continue;
			sprintf(buffer,"%s_hours",NrmQuarkToString(dimq));
			newdimq = NrmStringToQuark(buffer);
			nvstep = therec->internal_var_list;
			for (k = 0; k < therec->n_internal_vars; k++) {
				if (nvstep->int_var->var_info.var_name_quark == newdimq) {
					break;
				}
				nvstep = nvstep->next;
			}
			if (k == therec->n_internal_vars) {
				printf("var %s not found\n",NrmQuarkToString(newdimq));
				continue;
			}
			step->dim_inq->dim_name = newdimq;
		}
		step = step->next;
	}
	return;
}



static void _CreateSupplementaryTimeVariables
#if 	NhlNeedProto
(GribFileRecord *therec)
#else
(therec)
GribFileRecord *therec;
#endif
{
	GribDimInqRecList *step;
	GribInternalVarList *vstep;
	int i,j;

	step = therec->it_dims;
	for (i = 0; i < therec->n_it_dims; i++) {
		ng_size_t dimsize;
		NrmQuark *vals;
		double *dates;
		double *hours;
		char buffer[128];
		NrmQuark  *qstr;
		GribAttInqRecList *tmp_att_list_ptr= NULL;
		NclMultiDValData mdval;
			

		NrmQuark dimq = step->dim_inq->dim_name;
		vstep = therec->internal_var_list;
		for (j = 0; j < therec->n_internal_vars; j++) {
			if (vstep->int_var->var_info.var_name_quark == dimq) {
				break;
			}
			vstep = vstep->next;
		}
		if (j == therec->n_internal_vars) {
			printf("var %s no found\n",NrmQuarkToString(dimq));
			continue;
		}
		dimsize = vstep->int_var->value->multidval.totalelements;
		vals = (NrmQuark *)vstep->int_var->value->multidval.val;
		
		dates = _DateStringsToEncodedDoubles(vals,dimsize);
		hours = _DateStringsToHours(vals,dimsize);
		if (! (dates && hours) )
			continue;
		mdval = _NclCreateVal(NULL,NULL,Ncl_MultiDValData,0,(void*)dates,
			              NULL,1,&dimsize,TEMPORARY,NULL,nclTypedoubleClass),
		sprintf(buffer,"%s_encoded",NrmQuarkToString(dimq));
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*qstr = NrmStringToQuark
			("yyyymmddhh.hh_frac");
		GribPushAtt(&tmp_att_list_ptr,"units",qstr,1,nclTypestringClass); 
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*qstr =  NrmStringToQuark("initial time encoded as double");
		GribPushAtt(&tmp_att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
		_GribAddInternalVar(therec,NrmStringToQuark(buffer),
				    &step->dim_inq->dim_number,mdval,tmp_att_list_ptr,2);
		tmp_att_list_ptr = NULL;

		mdval = _NclCreateVal(NULL,NULL,Ncl_MultiDValData,0,(void*)hours,
			              NULL,1,&dimsize,TEMPORARY,NULL,nclTypedoubleClass),
		sprintf(buffer,"%s_hours",NrmQuarkToString(dimq));
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*qstr = NrmStringToQuark
			("hours since 1800-01-01 00:00");
		GribPushAtt(&tmp_att_list_ptr,"units",qstr,1,nclTypestringClass); 
		qstr = (NclQuark*)NclMalloc(sizeof(NclQuark));
		*qstr =  NrmStringToQuark("initial time");
		GribPushAtt(&tmp_att_list_ptr,"long_name",qstr,1,nclTypestringClass); 
		_GribAddInternalVar(therec,NrmStringToQuark(buffer),
				    &step->dim_inq->dim_number,mdval,tmp_att_list_ptr,2);
		tmp_att_list_ptr = NULL;
		step = step->next;
	}
	SetInitialTimeCoordinates(therec);
	return;
}
	
	

static void _SetFileDimsAndCoordVars
#if 	NhlNeedProto
(GribFileRecord *therec)
#else
(therec)
GribFileRecord *therec;
#endif
{
	GribParamList *step,*last,*tmpstep;
	char buffer[80];
	NclQuark gridx_q = NrmNULLQUARK,lat_q = NrmNULLQUARK;
	GribDimInqRecList *dstep,*ptr;
	GribDimInqRec *tmp;
	NclQuark *it_rhs, *it_lhs;
	int *rhs, *lhs;
	int *rhs1, *lhs1;
	float *rhs_f,*rhs_f1,*lhs_f,*lhs_f1;
	int i,j,m;
	int current_dim = 0;
	NclMultiDValData tmp_md;
	NclMultiDValData tmp_md1;
	NclGribFVarRec *test;
	int n_dims_lat = 0;
	int n_dims_lon = 0;
	int n_dims_rot = 0;
	ng_size_t *dimsizes_lat = NULL;
	ng_size_t *dimsizes_lon = NULL;
	ng_size_t *dimsizes_rot = NULL;
	float *tmp_lat = NULL;
	float *tmp_lon = NULL;
	float *tmp_rot = NULL;
	NhlErrorTypes is_err = NhlNOERROR;
	int tmp_file_dim_numbers[2];
	char name_buffer[80];
	GribAttInqRecList *att_list_ptr= NULL;
	GribAttInqRecList *tmp_att_list_ptr= NULL;
	NclQuark *tmp_string = NULL;
	float *tmp_float = NULL;
	int nlonatts = 0;
	int nlatatts = 0;
	int nrotatts = 0;
	GribAttInqRecList *lat_att_list_ptr = NULL;
	GribAttInqRecList *lon_att_list_ptr = NULL;
	GribAttInqRecList *rot_att_list_ptr = NULL;
	char *ens_name;
	int use_gds;
	GribRecordInqRec *grib_rec = NULL;

	therec->total_dims = 0;
	therec->n_scalar_dims = 0;
	therec->scalar_dims = NULL;
	therec->n_it_dims = 0;
	therec->it_dims = NULL;
	therec->n_ft_dims = 0;
	therec->ft_dims = NULL;
	therec->n_lv_dims = 0;
	therec->lv_dims = NULL;
	therec->n_grid_dims = 0;
	therec->grid_dims = NULL;
	step = therec->var_list;
/*
	step = NULL;
*/
	last = NULL;

	while(step != NULL) {

		for(i = 0; i < step->n_entries; i++) {
			if(step->thelist[i].rec_inq != NULL) {
				grib_rec = step->thelist[i].rec_inq;
				break;
			}
		}
		if (!grib_rec) {
			NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Variable contains no GRIB records");
			is_err = NhlFATAL;
		}

		current_dim = 0;
		step->aux_coords[0] = step->aux_coords[1] = NrmNULLQUARK;
		if (step->prob_param) {
			if(!step->ensemble_isatt) {
				dstep = therec->ensemble_dims;
				if (step->probability) { /* either a lower or an upper limit (but not both) */
					for(i = 0; i < therec->n_ensemble_dims; i++) {
						if(dstep->dim_inq->size == step->upper_probs->multidval.dim_sizes[0]) {
							tmp_md = _GribGetInternalVar(therec,dstep->dim_inq->dim_name,&test);
							if(tmp_md != NULL) {
								lhs_f = (float*)tmp_md->multidval.val;
								rhs_f = (float*)step->probability->multidval.val;
								j = 0;
								while(j<dstep->dim_inq->size) {
									if(lhs_f[j] != rhs_f[j]) {
										break;
									} else {
										j++;
									}
								}
								if(j == dstep->dim_inq->size) {
									break;
								} else {
									dstep= dstep->next;
								}
							} else {
								dstep  = dstep->next;
							}
						} else {
							dstep = dstep->next;
						}
					}
					if (dstep) {
						step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
						_NclDestroyObj((NclObj)step->probability);
						step->probability = NULL;
					}
					else {

                                        /*
					 * Need a new dimension entry w name and number
					 */
						tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
						tmp->dim_number = therec->total_dims;
						tmp->size = step->probability->multidval.dim_sizes[0];
						ens_name = "probability";
						sprintf(buffer,"%s_%s%d",step->prob_param->abrev,ens_name,therec->total_dims);
						tmp->dim_name = NrmStringToQuark(buffer);
						tmp->is_gds = -1;
						ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
						ptr->dim_inq = tmp;
						ptr->next = therec->ensemble_dims;
						therec->ensemble_dims = ptr;
						therec->n_ensemble_dims++;
						step->var_info.file_dim_num[current_dim] = tmp->dim_number;

						tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
						*tmp_string = NrmStringToQuark(step->prob_param->units);
						GribPushAtt(&tmp_att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

						sprintf(buffer,"%s probability limits",step->prob_param->long_name);
						tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
						*tmp_string = NrmStringToQuark(buffer);
						GribPushAtt(&tmp_att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 

						_GribAddInternalVar(therec,tmp->dim_name,&tmp->dim_number,
								    (NclMultiDValData)step->probability,tmp_att_list_ptr,2);
						tmp_att_list_ptr = NULL;
						step->probability = NULL;
						therec->total_dims++;
					}
					current_dim++;
				}
				else if (step->upper_probs && step->lower_probs) {
					for(i = 0; i < therec->n_ensemble_dims; i++) {
						if(dstep->dim_inq->size == step->probability->multidval.dim_sizes[0]) {
							sprintf(name_buffer,"%s%s",NrmQuarkToString(dstep->dim_inq->dim_name),"_upper");
							tmp_md = _GribGetInternalVar(therec,NrmStringToQuark(name_buffer),&test);
							sprintf(name_buffer,"%s%s",NrmQuarkToString(dstep->dim_inq->dim_name),"_lower");
							tmp_md1 = _GribGetInternalVar(therec,NrmStringToQuark(name_buffer),&test);
							j = 0;
							if((tmp_md != NULL )&&(tmp_md1 != NULL) ) {
								lhs_f = (float*)tmp_md->multidval.val;
								rhs_f = (float*)step->levels0->multidval.val;
								lhs_f1 = (float*)tmp_md1->multidval.val;
								rhs_f1 = (float*)step->levels1->multidval.val;
								while(j<dstep->dim_inq->size) {
									if((lhs_f[j] != rhs_f[j])||(lhs_f1[j] != rhs_f1[j])) {
										break;
									} else {
										j++;
									}
								}
							}
							if(j == dstep->dim_inq->size) {
								break;
							} else {
								dstep= dstep->next;
							}
						} else {
							dstep = dstep->next;
						}
					}
					if (dstep) {
						step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
						_NclDestroyObj((NclObj)step->upper_probs);
						step->upper_probs = NULL;
						_NclDestroyObj((NclObj)step->lower_probs);
						step->lower_probs = NULL;
					}
					else {

                                        /*
					 * Need a new dimension entry w name and number
					 */
						tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
						tmp->dim_number = therec->total_dims;
						tmp->size = step->probability->multidval.dim_sizes[0];
						ens_name = "probability";
						sprintf(buffer,"%s_%s%d",step->prob_param->abrev,ens_name,therec->total_dims);
						tmp->dim_name = NrmStringToQuark(buffer);
						tmp->is_gds = -1;
						ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
						ptr->dim_inq = tmp;
						ptr->next = therec->ensemble_dims;
						therec->ensemble_dims = ptr;
						therec->n_ensemble_dims++;
						step->var_info.file_dim_num[current_dim] = tmp->dim_number;

						tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
						*tmp_string = NrmStringToQuark(step->prob_param->units);
						GribPushAtt(&tmp_att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

						sprintf(buffer,"%s probability lower limits",step->prob_param->long_name);
						tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
						*tmp_string = NrmStringToQuark(buffer);
						GribPushAtt(&tmp_att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 
						sprintf(name_buffer,"%s%s",buffer,"_lower");

						_GribAddInternalVar(therec,NrmStringToQuark(name_buffer),&tmp->dim_number,
								    (NclMultiDValData)step->lower_probs,tmp_att_list_ptr,2);
						tmp_att_list_ptr = NULL;
						step->lower_probs = NULL;

						tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
						*tmp_string = NrmStringToQuark(step->prob_param->units);
						GribPushAtt(&tmp_att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

						sprintf(buffer,"%s probability upper limits",step->prob_param->long_name);
						tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
						*tmp_string = NrmStringToQuark(buffer);
						GribPushAtt(&tmp_att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 
						sprintf(name_buffer,"%s%s",buffer,"_upper");

						_GribAddInternalVar(therec,NrmStringToQuark(name_buffer),&tmp->dim_number,
								    (NclMultiDValData)step->upper_probs,tmp_att_list_ptr,2);
						tmp_att_list_ptr = NULL;
						step->upper_probs = NULL;

						therec->total_dims++;
					}
					current_dim++;
				}
			}
		}
		else {
			if(!step->ensemble_isatt) {
				dstep = therec->ensemble_dims;
				for(i = 0; i < therec->n_ensemble_dims; i++) {
					if(dstep->dim_inq->size == step->ensemble->multidval.dim_sizes[0]) {
						tmp_md = _GribGetInternalVar(therec,dstep->dim_inq->dim_name,&test);
						if(tmp_md != NULL) {
							lhs = (int*)tmp_md->multidval.val;
							rhs = (int*)step->ens_indexes->multidval.val;
							j = 0;
							while(j<dstep->dim_inq->size) {
								if(lhs[j] != rhs[j]) {
									break;
								} else {
									j++;
								}
							}
							if(j == dstep->dim_inq->size) {
								break;
							} else {
								dstep= dstep->next;
							}
						} else {
							dstep  = dstep->next;
						}
					} else {
						dstep = dstep->next;
					}
				}
				if(dstep == NULL) {
/*
 * Need a new dimension entry w name and number
 */
					tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
					tmp->dim_number = therec->total_dims;
					tmp->size = step->ensemble->multidval.dim_sizes[0];
					ens_name = "ensemble";
					sprintf(buffer,"%s%d",ens_name,therec->total_dims);
					tmp->dim_name = NrmStringToQuark(buffer);
					tmp->is_gds = -1;
					ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
					ptr->dim_inq = tmp;
					ptr->next = therec->ensemble_dims;
					therec->ensemble_dims = ptr;
					therec->n_ensemble_dims++;
					step->var_info.file_dim_num[current_dim] = tmp->dim_number;

					tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
					*tmp_string = NrmStringToQuark("non-dim");
					GribPushAtt(&tmp_att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

					sprintf(buffer,"%s indexes",ens_name);
					tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
					*tmp_string = NrmStringToQuark(buffer);
					GribPushAtt(&tmp_att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 

					_GribAddInternalVar(therec,tmp->dim_name,&tmp->dim_number,
							    (NclMultiDValData)step->ens_indexes,tmp_att_list_ptr,2);
					tmp_att_list_ptr = NULL;
					step->ens_indexes = NULL;

					tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
					sprintf(buffer,"%s elements description",ens_name);
					*tmp_string = NrmStringToQuark(buffer);
					GribPushAtt(&tmp_att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 

					sprintf(buffer,"%s%d",ens_name,therec->total_dims);
					sprintf(&(buffer[strlen(buffer)]),"_info");
					_GribAddInternalVar(therec,NrmStringToQuark(buffer),&tmp->dim_number,
							    (NclMultiDValData)step->ensemble,tmp_att_list_ptr,1);
					tmp_att_list_ptr = NULL;
					step->ensemble = NULL;
					therec->total_dims++;
				} else {
					step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
					_NclDestroyObj((NclObj)step->ens_indexes);
					step->ens_indexes = NULL;
					_NclDestroyObj((NclObj)step->ensemble);
					step->ensemble = NULL;
				}
				current_dim++;
			}
		}
		if(!step->yymmddhh_isatt) {
			dstep = therec->it_dims;
			for(i = 0; i < therec->n_it_dims; i++) {
				if(dstep->dim_inq->size == step->yymmddhh->multidval.dim_sizes[0]) {
					tmp_md = _GribGetInternalVar(therec,dstep->dim_inq->dim_name,&test);
					if(tmp_md != NULL) {
						it_lhs = (NclQuark*)tmp_md->multidval.val;
	
						it_rhs = (NclQuark*)step->yymmddhh->multidval.val;
						j = 0;
						while(j<dstep->dim_inq->size) {
							if(it_lhs[j] != it_rhs[j]) {
								break;
							} else {
								j++;
							}
						}
						if(j == dstep->dim_inq->size) {
							break;
						} else {
							dstep= dstep->next;
						}
					} else {
						dstep = dstep->next;
					}
				} else {
					dstep = dstep->next;
				}
			}
/*
* All pointers to coordate will end up in dim list not in param list
*/
			if(dstep == NULL) {
/*
* Need a new dimension entry w name and number
*/
				tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
				tmp->dim_number = therec->total_dims;
				tmp->size = step->yymmddhh->multidval.dim_sizes[0];
				sprintf(buffer,"initial_time%d",therec->total_dims);
				tmp->dim_name = NrmStringToQuark(buffer);
				tmp->is_gds = -1;
				therec->total_dims++;
				ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
				ptr->dim_inq = tmp;
				ptr->next = therec->it_dims;
				therec->it_dims = ptr;
				therec->n_it_dims++;
				step->var_info.file_dim_num[current_dim] = tmp->dim_number;
				
				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				*tmp_string = NrmStringToQuark("mm/dd/yyyy (hh:mm)");
				GribPushAtt(&tmp_att_list_ptr,"units",tmp_string,1,nclTypestringClass); 
				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				*tmp_string = NrmStringToQuark("Initial time of first record");
				GribPushAtt(&tmp_att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 

				_GribAddInternalVar(therec,tmp->dim_name,&tmp->dim_number,(NclMultiDValData)step->yymmddhh,tmp_att_list_ptr,2);
				tmp_att_list_ptr = NULL;
				step->yymmddhh = NULL;
			} else {
				step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
				_NclDestroyObj((NclObj)step->yymmddhh);
				step->yymmddhh = NULL;
			}
			current_dim++;
		}
		if(!step->forecast_time_isatt) {
			dstep = therec->ft_dims;
			for(i = 0; i < therec->n_ft_dims; i++) {
				if(dstep->dim_inq->sub_type_id == step->time_unit_indicator &&
				   dstep->dim_inq->size == step->forecast_time->multidval.dim_sizes[0]) {
					tmp_md = _GribGetInternalVar(therec,dstep->dim_inq->dim_name,&test);
					if(tmp_md != NULL) {
						lhs = (int*)tmp_md->multidval.val;
						rhs = (int*)step->forecast_time->multidval.val;
						j = 0;
						while(j<dstep->dim_inq->size) {
							if(lhs[j] != rhs[j]) {
								break;
							} else {
								j++;
							}
						}
						if(j == dstep->dim_inq->size) {
							break;
						} else {
							dstep= dstep->next;
						}
					} else {
						dstep  = dstep->next;
					}
				} else {
					dstep = dstep->next;
				}
			}
			if(dstep == NULL) {
/*
* Need a new dimension entry w name and number
*/
				tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
				tmp->dim_number = therec->total_dims;
				tmp->size = step->forecast_time->multidval.dim_sizes[0];
				sprintf(buffer,"forecast_time%d",therec->total_dims);
				tmp->dim_name = NrmStringToQuark(buffer);
				tmp->is_gds = -1;
				tmp->sub_type_id = step->time_unit_indicator;
				therec->total_dims++;
				ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
				ptr->dim_inq = tmp;
				ptr->next = therec->ft_dims;
				therec->ft_dims = ptr;
				therec->n_ft_dims++;
				step->var_info.file_dim_num[current_dim] = tmp->dim_number;

				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				*tmp_string = ForecastTimeUnitsToQuark(step->time_unit_indicator);
				GribPushAtt(&tmp_att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				*tmp_string = NrmStringToQuark("Forecast offset from initial time");
				GribPushAtt(&tmp_att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 

				_GribAddInternalVar(therec,tmp->dim_name,&tmp->dim_number,(NclMultiDValData)step->forecast_time,tmp_att_list_ptr,2);
				tmp_att_list_ptr = NULL;
				step->forecast_time = NULL;
			} else {
				step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
				_NclDestroyObj((NclObj)step->forecast_time);
				step->forecast_time = NULL;
			}
			current_dim++;
		}

		if((!step->levels_isatt)&& (step->levels!=NULL)) {
			dstep = therec->lv_dims;
			for(i = 0; i < therec->n_lv_dims; i++) {
				if(dstep->dim_inq->sub_type_id == step->level_indicator &&
				   dstep->dim_inq->size == step->levels->multidval.dim_sizes[0]) {
					tmp_md = _GribGetInternalVar(therec,dstep->dim_inq->dim_name,&test);
					if(tmp_md != NULL ) {
						lhs = (int*)tmp_md->multidval.val;
						rhs = (int*)step->levels->multidval.val;
						j = 0;
						while(j<dstep->dim_inq->size) {
							if(lhs[j] != rhs[j]) {
								break;
							} else {
								j++;
							}
						}
						if(j == dstep->dim_inq->size) {
							break;
						} else {
							dstep= dstep->next;
						}
					} else {
						dstep= dstep->next;
					}
				} else {
					dstep = dstep->next;
				}
			}
			if(dstep == NULL) {
/*
* Need a new dimension entry w name and number
*/
				tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
				tmp->dim_number = therec->total_dims;
				tmp->sub_type_id = step->level_indicator;
				tmp->is_gds = -1;
				tmp->size = step->levels->multidval.dim_sizes[0];
				for(i = 0; i < sizeof(level_index)/sizeof(int); i++) {
					if(level_index[i] == step->level_indicator) {
						break;
					}
				}
				if(i < sizeof(level_index)/sizeof(int)) {
					sprintf(buffer,"lv_%s%d",level_str[i],therec->total_dims);
				} else {
					sprintf(buffer,"levels%d",therec->total_dims);
				}
			
				tmp->dim_name = NrmStringToQuark(buffer);
				therec->total_dims++;
				ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
				ptr->dim_inq = tmp;
				ptr->next = therec->lv_dims;
				therec->lv_dims = ptr;
				therec->n_lv_dims++;
				step->var_info.file_dim_num[current_dim] = tmp->dim_number;

				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				if(i < sizeof(level_index)/sizeof(int)) {
					*tmp_string = NrmStringToQuark(level_units_str[i]);
				} else {
					*tmp_string = NrmStringToQuark("unknown");
				}
				GribPushAtt(&att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				if(i < sizeof(level_index)/sizeof(int)) {
					*tmp_string = NrmStringToQuark(level_str_long_name[i]);
				} else {
					*tmp_string = NrmStringToQuark("unknown");
				}
				GribPushAtt(&att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 
				

				_GribAddInternalVar(therec,tmp->dim_name,&tmp->dim_number,(NclMultiDValData)step->levels,att_list_ptr,2);
				att_list_ptr = NULL;
				step->levels = NULL;
			} else {
				step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
				_NclDestroyObj((NclObj)step->levels);
				step->levels = NULL;
			}
			current_dim++;
		} else if((!step->levels_isatt)&& (step->levels0 !=NULL)&&(step->levels1 !=NULL)) {

			dstep = therec->lv_dims;
			for(i = 0; i < therec->n_lv_dims; i++) {
				if(dstep->dim_inq->sub_type_id == step->level_indicator &&
				   dstep->dim_inq->size == step->levels0->multidval.dim_sizes[0]) {
					sprintf(name_buffer,"%s%s",NrmQuarkToString(dstep->dim_inq->dim_name),"_l0");
					tmp_md = _GribGetInternalVar(therec,NrmStringToQuark(name_buffer),&test);
					sprintf(name_buffer,"%s%s",NrmQuarkToString(dstep->dim_inq->dim_name),"_l1");
					tmp_md1 = _GribGetInternalVar(therec,NrmStringToQuark(name_buffer),&test);
					if((tmp_md != NULL )&&(tmp_md1 != NULL) ) {
						lhs = (int*)tmp_md->multidval.val;
						rhs = (int*)step->levels0->multidval.val;
						lhs1 = (int*)tmp_md1->multidval.val;
						rhs1 = (int*)step->levels1->multidval.val;
						j = 0;
						while(j<dstep->dim_inq->size) {
							if((lhs[j] != rhs[j])||(lhs1[j] != rhs1[j])) {
								break;
							} else {
								j++;
							}
						}
						if(j == dstep->dim_inq->size) {
							break;
						} else {
							dstep= dstep->next;
						}
					} else {
						dstep= dstep->next;
					}
				} else {
					dstep = dstep->next;
				}
			}
			if(dstep == NULL) {
/*
* Need a new dimension entry w name and number
*/
				tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
				tmp->sub_type_id = step->level_indicator;
				tmp->dim_number = therec->total_dims;
				tmp->is_gds = -1;
				tmp->size = step->levels0->multidval.dim_sizes[0];
				for(i = 0; i < sizeof(level_index)/sizeof(int); i++) {
					if(level_index[i] == step->level_indicator) {
						break;
					}
				}
				if(i < sizeof(level_index)/sizeof(int)) {
					sprintf(buffer,"lv_%s%d",level_str[i],therec->total_dims);
				} else {
					sprintf(buffer,"levels%d",therec->total_dims);
				}
			
				tmp->dim_name = NrmStringToQuark(buffer);
				therec->total_dims++;
				ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
				ptr->dim_inq = tmp;
				ptr->next = therec->lv_dims;
				therec->lv_dims = ptr;
				therec->n_lv_dims++;
				step->var_info.file_dim_num[current_dim] = tmp->dim_number;
				sprintf(name_buffer,"%s%s",buffer,"_l0");

				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				if(i < sizeof(level_index)/sizeof(int)) {
					*tmp_string = NrmStringToQuark(level_units_str[i]);
				} else {
					*tmp_string = NrmStringToQuark("unknown");
				}
				GribPushAtt(&att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				if(i < sizeof(level_index)/sizeof(int)) {
					*tmp_string = NrmStringToQuark(level_str_long_name[i]);
				} else {
					*tmp_string = NrmStringToQuark("unknown");
				}
				GribPushAtt(&att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 

				_GribAddInternalVar(therec,NrmStringToQuark(name_buffer),&tmp->dim_number,(NclMultiDValData)step->levels0,att_list_ptr,2);

				att_list_ptr = NULL;

				sprintf(name_buffer,"%s%s",buffer,"_l1");
				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				if(i < sizeof(level_index)/sizeof(int)) {
					*tmp_string = NrmStringToQuark(level_units_str[i]);
				} else {
					*tmp_string = NrmStringToQuark("unknown");
				}
				GribPushAtt(&att_list_ptr,"units",tmp_string,1,nclTypestringClass); 

				tmp_string = (NclQuark*)NclMalloc(sizeof(NclQuark));
				if(i < sizeof(level_index)/sizeof(int)) {
					*tmp_string = NrmStringToQuark(level_str_long_name[i]);
				} else {
					*tmp_string = NrmStringToQuark("unknown");
				}
				GribPushAtt(&att_list_ptr,"long_name",tmp_string,1,nclTypestringClass); 
				_GribAddInternalVar(therec,NrmStringToQuark(name_buffer),&tmp->dim_number,(NclMultiDValData)step->levels1,att_list_ptr,2);


				att_list_ptr = NULL;
				step->levels0 = NULL;
				step->levels1 = NULL;
			} else {
				step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
				_NclDestroyObj((NclObj)step->levels0);
				_NclDestroyObj((NclObj)step->levels1);
				step->levels0 = NULL;
				step->levels1 = NULL;
			}
			current_dim++;
		}
		if(step->level_indicator == 109) {
			/* 
			 * This is for hybrid levels: if interface levels then an extra dimension is
			 * required. It should follow directly after the corresponding level center 
			 * dimension.
			 */
			_Do109(therec,step);
		}

/*
* Now its time to get the grid coordinates  and define grid variables
* First switch on whether record has GDS or not
* if not :
*  check to see if dimensions are defined
*  if not: check get_grid field
*     then get_grid.
*     if its more that 1D then
*       define gridx_### and gridy_### and then add variables gridlat_### and gridlon_### if gridx and gridy are 
*     else
*       define lat_### and lon_### and add them as variables of the same name (real coordinate variables)
*/

		use_gds = step->has_gds && (step->grid_number == 0 || step->grid_number == 255); 
		if((step->grid_tbl_index != -1)&&(grid[step->grid_tbl_index].get_grid != NULL)) {
/* 
* Search for both gridlat_## and gridx_## grid number will always be added in sequence so finding lat or x means
* you have the lon and y file dimension numbers.
*/
			sprintf(buffer,"gridx_%d",step->grid_number);
			gridx_q = NrmStringToQuark(buffer);
			sprintf(buffer,"lat_%d",step->grid_number);
			lat_q = NrmStringToQuark(buffer);
		} else if((step->has_gds)&&(step->grid_gds_tbl_index != -1)/*&&(step->grid_number == 255)*/) { 
			
			if((grid_gds[step->grid_gds_tbl_index].un_pack == NULL)&&(grid_gds[step->grid_gds_tbl_index].get_gds_grid == NULL)) {
				NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Parameter (%d) has an unsupported GDS type (%d), GDS's are not currently supported",step->param_number,step->gds_type);
				is_err = NhlFATAL;
			}
		} else {
			if (use_gds) {
				NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Unsupported GDS grid type (%d) can't decode",step->gds_type);
			}
			else {
				NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Unsupported grid number (%d) can't decode",step->grid_number);
			}
			is_err = NhlWARNING;
		}
		if(is_err == NhlNOERROR) {

			if((step->has_gds)&&(step->grid_gds_tbl_index != -1)/*&&(step->grid_number == 255)*/) {
/*
* For gds grid it must be decoded every time since several different grid could be defined
* by the smae grid_type number.
*/
				dstep = therec->grid_dims;
				while(dstep != NULL) {
					if((dstep->dim_inq->is_gds == step->gds_type)&&
						(GdsCompare(dstep->dim_inq->gds,dstep->dim_inq->gds_size,
							    grib_rec->gds,grib_rec->gds_size))) {
						if ((step->gds_type == 203) && (dstep->dim_inq->is_uv != Is_UV(step->param_number))) {
							dstep = dstep->next;
							continue;
						}
						break;
					} else {
						dstep = dstep->next;
					}
				}
			}  else {
				dstep = therec->grid_dims;
				while(dstep != NULL) {
					if((dstep->dim_inq->dim_name == gridx_q )||(dstep->dim_inq->dim_name == lat_q)) {
						break;
					} else {
						dstep = dstep->next;
					}
				}
			}
			if(dstep == NULL) {
				nlonatts = 0;
				nlatatts = 0;
				nrotatts = 0;
				lat_att_list_ptr = NULL;
				lon_att_list_ptr = NULL;
				rot_att_list_ptr = NULL;
				tmp_rot = NULL;
				tmp_lon = NULL;
				tmp_lat = NULL;
				n_dims_lat = 0;
				n_dims_lon = 0;
				dimsizes_lat = NULL;
				dimsizes_lon = NULL;

/*
* Grid has not been defined
*/
				if(step->grid_tbl_index!=-1) {
					int do_rot;
#if 0
					if (grib_rec->has_gds) {
						printf("vector rotation %s for pre-defined grid %d\n",((unsigned char)010 & grib_rec->gds[16]) ? 
						       "grid relative" : "earth relative", step->grid_number);
					}
#endif
					if (grid[step->grid_tbl_index].get_grid != NULL) {
						(*grid[step->grid_tbl_index].get_grid)(step,&tmp_lat,&n_dims_lat,&dimsizes_lat,
										       &tmp_lon,&n_dims_lon,&dimsizes_lon,&tmp_rot,
										       &lat_att_list_ptr,&nlatatts,&lon_att_list_ptr,&nlonatts,
										       &rot_att_list_ptr,&nrotatts);
					}

					/* Get the atts if a grid has been set up but the atts have not been defined yet 
					   -- the new way is to set them in the 'grid' function */

					if (!tmp_lat) 
						step->grid_tbl_index = -1;
					else if(((grid[step->grid_tbl_index].get_grid_atts) != NULL) && nlatatts == 0) {
						int grid_oriented;
						do_rot = tmp_rot == NULL ? 0 : 1;
						/*
						 * if there's a gds, gds[16] determines whether the uv rotation is 
						 * grid or earth based; otherwise assume that if a rotation variable was
						 * created the rotation is grid-based
						 */

						if (step->has_gds)
							grid_oriented = (grib_rec->gds[16] & 010 )  ? 1 : 0;
						else
							grid_oriented = do_rot;

						(*grid[step->grid_tbl_index].get_grid_atts)(step,&lat_att_list_ptr,&nlatatts,
											    &lon_att_list_ptr,&nlonatts,
											    do_rot,grid_oriented,
											    &rot_att_list_ptr,&nrotatts);
					}
				}
				/* if a pre-defined grid has not been set up and there is a gds grid type that applies do this */

				if (tmp_lat == NULL && step->grid_gds_tbl_index != -1) {
#if 0
					printf("vector rotation %s for gds grid %d\n",((unsigned char)010 & grib_rec->gds[16]) ? 
					       "grid relative" : "earth relative", step->gds_type);
#endif
					(*grid_gds[step->grid_gds_tbl_index].get_gds_grid)
						(step,&tmp_lat,&n_dims_lat,&dimsizes_lat,&tmp_lon,&n_dims_lon,&dimsizes_lon,
						 &tmp_rot,&n_dims_rot,&dimsizes_rot,
						 &lat_att_list_ptr,&nlatatts,&lon_att_list_ptr,&nlonatts,&rot_att_list_ptr,&nrotatts);
				}

	/*
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	* Grids always need to be inserted into the grid_dim list in the right order. First lon is pushed then lat so that dstep->dim_inq
	* always points to lat and dstep->next->dim_inq point to lon
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	*/
		
				m = 0;
				while((m<step->n_entries)&&(step->thelist[m].rec_inq == NULL)) m++;
				if((n_dims_lon == 1)&&(n_dims_lat == 1)) {
					if((step->has_gds )&& (step->gds_type == 50)) {
						step->var_info.dim_sizes[current_dim] = 2;
						step->var_info.dim_sizes[current_dim+1] = dimsizes_lat[0];
						step->var_info.dim_sizes[current_dim+2] = dimsizes_lon[0];
						step->var_info.file_dim_num[current_dim] = therec->total_dims;
						step->var_info.file_dim_num[current_dim+1] = therec->total_dims+1;
						step->var_info.file_dim_num[current_dim+2] = therec->total_dims + 2;
						sprintf(buffer,"real_imaginary");
						tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
						tmp->dim_number = therec->total_dims;
						tmp->size = 2;
						tmp->dim_name = NrmStringToQuark(buffer);
						tmp->is_gds = step->gds_type;
						tmp->gds = (unsigned char*)NclMalloc(step->thelist[m].rec_inq->gds_size);


						tmp->gds_size = step->thelist[m].rec_inq->gds_size;
						memcpy(tmp->gds,step->thelist[m].rec_inq->gds,step->thelist[m].rec_inq->gds_size);
						ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
						ptr->dim_inq= tmp;
						ptr->next = therec->grid_dims;
						therec->grid_dims = ptr;
						therec->n_grid_dims++;
						step->var_info.doff = 2;
					} else {
						step->var_info.dim_sizes[current_dim] = dimsizes_lat[0];
						step->var_info.dim_sizes[current_dim+1] = dimsizes_lon[0];
						step->var_info.file_dim_num[current_dim] = therec->total_dims;
						step->var_info.file_dim_num[current_dim+1] = therec->total_dims + 1;
						step->var_info.doff = 1;
					}
					/*
					 * y or lon first!
					 */
					if(use_gds) {
						sprintf(buffer,"g%d_lon_%d",step->gds_type,therec->total_dims+step->var_info.doff);
					} else {
						sprintf(buffer,"lon_%d",step->grid_number);
					}
					tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
					tmp->dim_number = therec->total_dims + step->var_info.doff;
					tmp->size = dimsizes_lon[0];
					tmp->dim_name = NrmStringToQuark(buffer);
					tmp->is_gds = step->gds_type;
					tmp->gds_size = step->thelist[m].rec_inq->gds_size;
					tmp->gds = (unsigned char*)NclMalloc(step->thelist[m].rec_inq->gds_size);
					memcpy(tmp->gds,step->thelist[m].rec_inq->gds,step->thelist[m].rec_inq->gds_size);
					ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
					ptr->dim_inq= tmp;
					ptr->next = therec->grid_dims;
					therec->grid_dims = ptr;
					therec->n_grid_dims++;
					/*	
					  not valid for single dimensioned coords
					  tmp_float = NclMalloc((unsigned)sizeof(float)*2);
					  tmp_float[0] = tmp_lon[0];
					  tmp_float[1] = tmp_lon[dimsizes_lon[0]-1];
					  GribPushAtt(&lon_att_list_ptr,"corners",tmp_float,2,nclTypefloatClass); nlonatts++;
					*/

					
					if(tmp_lon != NULL) {	
						_GribAddInternalVar(therec,tmp->dim_name,&tmp->dim_number,
								    (NclMultiDValData)_NclCreateVal(
									    NULL,
									    NULL,
									    Ncl_MultiDValData,
									    0,
									    (void*)tmp_lon,
									    NULL,
									    n_dims_lon,
									    dimsizes_lon,
									    TEMPORARY,
									    NULL,
									    nclTypefloatClass),
								    lon_att_list_ptr,nlonatts);
					}
					NclFree(dimsizes_lon);
					if(use_gds) {
						sprintf(buffer,"g%d_lat_%d",step->gds_type,therec->total_dims + (step->var_info.doff - 1));
					} else {
						sprintf(buffer,"lat_%d",step->grid_number);
					}
					tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
					tmp->dim_number = therec->total_dims + (step->var_info.doff - 1);
					tmp->size = dimsizes_lat[0];
					tmp->dim_name = NrmStringToQuark(buffer);
					tmp->is_gds = step->gds_type;
					tmp->gds_size = step->thelist[m].rec_inq->gds_size;
					tmp->gds = (unsigned char*)NclMalloc(step->thelist[m].rec_inq->gds_size);
					memcpy(tmp->gds,step->thelist[m].rec_inq->gds,step->thelist[m].rec_inq->gds_size);
					ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
					ptr->dim_inq= tmp;
					ptr->next = therec->grid_dims;
					therec->grid_dims = ptr;
					therec->n_grid_dims++;

					/*	
					  not valid for single dimensioned coords
					  tmp_float = NclMalloc((unsigned)sizeof(float)*2);
					  tmp_float[0] = tmp_lat[0];
					  tmp_float[1] = tmp_lat[dimsizes_lat[0]-1];
					  GribPushAtt(&lat_att_list_ptr,"corners",tmp_float,2,nclTypefloatClass); nlatatts++;
					*/
					if(tmp_lat != NULL) {
						_GribAddInternalVar(therec,tmp->dim_name,&tmp->dim_number,
								    (NclMultiDValData)_NclCreateVal(
									    NULL,
									    NULL,
									    Ncl_MultiDValData,
									    0,
									    (void*)tmp_lat,
									    NULL,
									    n_dims_lat,
									    dimsizes_lat,
									    TEMPORARY,
									    NULL,
									    nclTypefloatClass),
								    lat_att_list_ptr,nlatatts);
					}
					NclFree(dimsizes_lat);
					therec->total_dims += step->var_info.doff+1;


				} else if((n_dims_lon ==2)&&(n_dims_lat ==2)&&(dimsizes_lat[0] == dimsizes_lon[0])&&(dimsizes_lat[1] == dimsizes_lon[1])) {
					char *uv_m = "m";
					step->var_info.dim_sizes[current_dim] = dimsizes_lat[0];
					step->var_info.dim_sizes[current_dim+1] = dimsizes_lon[1];
					step->var_info.file_dim_num[current_dim] = therec->total_dims;
					step->var_info.file_dim_num[current_dim+1] = therec->total_dims + 1;
					step->var_info.doff=1;

					if(use_gds) {
						if (step->gds_type != 203) {
							sprintf(buffer,"g%d_y_%d",step->gds_type,therec->total_dims + 1);
						}
						else {
							if (Is_UV(step->param_number))
								uv_m = "v";
							sprintf(buffer,"g%d%s_y_%d",step->gds_type,uv_m,therec->total_dims + 1);
						}
					} else {
						if (step->gds_type != 203) {
							sprintf(buffer,"gridy_%d",step->grid_number);
						}
						else {
							if (Is_UV(step->param_number))
								uv_m = "v";
							sprintf(buffer,"gridy_%d%s",step->grid_number,uv_m);
						}
					}
					tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
					tmp->dim_number = therec->total_dims + 1;
					tmp->size = dimsizes_lon[1];
					tmp->dim_name = NrmStringToQuark(buffer);
					tmp->is_gds = step->gds_type;
					tmp->is_uv = step->gds_type == 203 && Is_UV(step->param_number);
					tmp->gds_size = step->thelist[m].rec_inq->gds_size;
					tmp->gds = (unsigned char*)NclMalloc(step->thelist[m].rec_inq->gds_size);
					memcpy(tmp->gds,step->thelist[m].rec_inq->gds,step->thelist[m].rec_inq->gds_size);
					ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
					ptr->dim_inq= tmp;
					ptr->next = therec->grid_dims;
					therec->grid_dims = ptr;
					therec->n_grid_dims++;
					if (use_gds) {
						if (step->gds_type != 203) {
							sprintf(buffer,"g%d_lon_%d",step->gds_type,therec->total_dims + 1);
						}
						else {
							sprintf(buffer,"g%d%s_lon_%d",step->gds_type,uv_m,therec->total_dims + 1);
						}
					} else {
						if (step->gds_type != 203) {
							sprintf(buffer,"gridlon_%d",step->grid_number);
						}
						else {
							sprintf(buffer,"gridlon_%d%s",step->grid_number,uv_m);
						}
					}
					step->aux_coords[1] = NrmStringToQuark(buffer);
					tmp_file_dim_numbers[0] = therec->total_dims;
					tmp_file_dim_numbers[1] = therec->total_dims+ 1;

					tmp_float = NclMalloc((unsigned)sizeof(float)*4);
					tmp_float[0] = tmp_lon[0];
					tmp_float[1] = tmp_lon[dimsizes_lon[1]-1];
					tmp_float[3] = tmp_lon[(dimsizes_lon[0]-1) * dimsizes_lon[1]];
					tmp_float[2] = tmp_lon[(dimsizes_lon[0] * dimsizes_lon[1])-1];
					GribPushAtt(&lon_att_list_ptr,"corners",tmp_float,4,nclTypefloatClass); nlonatts++;

					_GribAddInternalVar(therec,NrmStringToQuark(buffer),tmp_file_dim_numbers,
							    (NclMultiDValData)_NclCreateVal(
								    NULL,
								    NULL,
								    Ncl_MultiDValData,
								    0,
								    (void*)tmp_lon,
								    NULL,
								    n_dims_lon,
								    dimsizes_lon,
								    TEMPORARY,
								    NULL,
								    nclTypefloatClass),
							    lon_att_list_ptr,nlonatts);
					NclFree(dimsizes_lon);
						
					if (use_gds) {
						if (step->gds_type != 203) {
							sprintf(buffer,"g%d_x_%d",step->gds_type,therec->total_dims);
						}
						else {
							sprintf(buffer,"g%d%s_x_%d",step->gds_type,uv_m,therec->total_dims);
						}
					} else {
						if (step->gds_type != 203) {
							sprintf(buffer,"gridx_%d",step->grid_number);
						}
						else {
							if (Is_UV(step->param_number))
								uv_m = "v";
							sprintf(buffer,"gridx_%d%s",step->grid_number,uv_m);
						}
					}
					tmp = (GribDimInqRec*)NclMalloc((unsigned)sizeof(GribDimInqRec));
					tmp->dim_number = therec->total_dims;
					tmp->size = dimsizes_lat[0];
					tmp->dim_name = NrmStringToQuark(buffer);
					tmp->is_gds = step->gds_type;
					tmp->is_uv = step->gds_type == 203 && Is_UV(step->param_number);
					tmp->gds_size = step->thelist[m].rec_inq->gds_size;
					tmp->gds = (unsigned char*)NclMalloc(step->thelist[m].rec_inq->gds_size);
					memcpy(tmp->gds,step->thelist[m].rec_inq->gds,step->thelist[m].rec_inq->gds_size);
					ptr = (GribDimInqRecList*)NclMalloc((unsigned)sizeof(GribDimInqRecList));
					ptr->dim_inq= tmp;
					ptr->next = therec->grid_dims;
					therec->grid_dims = ptr;
					therec->n_grid_dims++;
					if (use_gds) {
						if (step->gds_type != 203) {
							sprintf(buffer,"g%d_lat_%d",step->gds_type,therec->total_dims);
						}
						else {
							sprintf(buffer,"g%d%s_lat_%d",step->gds_type,uv_m,therec->total_dims);
						}
					} else {
						if (step->gds_type != 203) {
							sprintf(buffer,"gridlat_%d",step->grid_number);
						}
						else {
							sprintf(buffer,"gridlat_%d%s",step->grid_number,uv_m);
						}
					}
					step->aux_coords[0] = NrmStringToQuark(buffer);
					tmp_float = NclMalloc((unsigned)sizeof(float)*4);
					tmp_float[0] = tmp_lat[0];
					tmp_float[1] = tmp_lat[dimsizes_lat[1]-1];
					tmp_float[3] = tmp_lat[(dimsizes_lat[0]-1) * dimsizes_lat[1]];
					tmp_float[2] = tmp_lat[(dimsizes_lat[0] * dimsizes_lat[1])-1];
					GribPushAtt(&lat_att_list_ptr,"corners",tmp_float,4,nclTypefloatClass); nlatatts++;

					_GribAddInternalVar(therec,NrmStringToQuark(buffer),tmp_file_dim_numbers,(NclMultiDValData)_NclCreateVal(
								    NULL,
								    NULL,
								    Ncl_MultiDValData,
								    0,
								    (void*)tmp_lat,
								    NULL,
								    n_dims_lat,
								    dimsizes_lat,
								    TEMPORARY,
								    NULL,
								    nclTypefloatClass),lat_att_list_ptr,nlatatts);

					therec->total_dims += 2;
					if (tmp_rot != NULL) {
						/* the rotation array is assumed to be the same size as the lat and lon arrays */
						if (use_gds) {
							if (step->gds_type != 203) {
								sprintf(buffer,"g%d_rot_%d",step->gds_type,therec->total_dims);
							}
							else {
								sprintf(buffer,"g%d%s_rot_%d",step->gds_type,uv_m,therec->total_dims);
							}
						} else {
							if (step->gds_type != 203) {
								sprintf(buffer,"gridrot_%d",step->grid_number);
							}
							else {
								sprintf(buffer,"gridrot_%d%s",step->grid_number,uv_m);
							}
						}
						_GribAddInternalVar(therec,NrmStringToQuark(buffer),tmp_file_dim_numbers,
								    (NclMultiDValData)_NclCreateVal(NULL,
												    NULL,
												    Ncl_MultiDValData,
												    0,
												    (void*)tmp_rot,
												    NULL,
												    n_dims_lat,
												    dimsizes_lat,
												    TEMPORARY,
												    NULL,
												    nclTypefloatClass),rot_att_list_ptr,nrotatts);
					}
					NclFree(dimsizes_lat);
				} else {
					NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Couldn't handle dimension information returned by grid decoding");
					is_err = NhlFATAL;
				}
				if (is_err == NhlNOERROR) {
				        if((step->has_gds )&& (step->gds_type == 50)) {
				                _NewSHGridCache(therec,step);
				        } else {
					        _NewGridCache(therec,step);
						}
				}
			} else {
				GribInternalVarList	*iv;
				int dnum1, dnum2;
				int count = 0;
				if(dstep->dim_inq->is_gds==50) {
					step->var_info.dim_sizes[current_dim+1] = dstep->dim_inq->size;
					dnum1 = step->var_info.file_dim_num[current_dim+1] = dstep->dim_inq->dim_number;
					step->var_info.dim_sizes[current_dim+2] = dstep->next->dim_inq->size;
					dnum2 = step->var_info.file_dim_num[current_dim+2] = dstep->next->dim_inq->dim_number;
					step->var_info.dim_sizes[current_dim] = 2;
					step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number-1;
					step->var_info.doff = 2;
				} else {
					step->var_info.dim_sizes[current_dim] = dstep->dim_inq->size;
					dnum1 = step->var_info.file_dim_num[current_dim] = dstep->dim_inq->dim_number;
					step->var_info.dim_sizes[current_dim+1] = dstep->next->dim_inq->size;
					dnum2 = step->var_info.file_dim_num[current_dim+1] = dstep->next->dim_inq->dim_number;
					step->var_info.doff = 1;
				}
				/* find the auxiliary coordinate variables if they exist */
				for (iv = therec->internal_var_list; iv != NULL; iv = iv->next) {
					if (iv->int_var->var_info.num_dimensions != 2)
						continue;
					if ( !(iv->int_var->var_info.file_dim_num[0] == dnum1 &&
					       iv->int_var->var_info.file_dim_num[1] == dnum2)) 
						continue;
					if (strstr(NrmQuarkToString(iv->int_var->var_info.var_name_quark),"rot"))
						continue;
					step->aux_coords[count] = iv->int_var->var_info.var_name_quark;
					count++;
					if (count == 2) {
						break;
					}
				}
			}
		}
		if(is_err > NhlFATAL) {

			last = step;	
			step = step->next;
		} else {
			NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Deleting reference to parameter because of decoding error");
			is_err = NhlNOERROR;
			if(last != NULL) {
				last->next = step->next;
			} else {
				therec->var_list = step->next;
			}
			tmpstep = step;
			step = step->next;
			_GribFreeParamRec(tmpstep);
			therec->n_vars--;
		}
	}

	_CreateSupplementaryTimeVariables(therec);

	return;
}

static ITLIST *GetITList
#if 	NhlNeedProto
(GribFileRecord *therec,
 GribParamList *thevar,
 GribRecordInqRecList *step,
 int* n_it, GIT **it_vals,
 int* n_ft,int **ft_vals,
 int* total_valid_lv ,int** valid_lv_vals,int** valid_lv_vals1)
#else
(therec,thevar,step, n_it,it_vals,n_ft, ft_vals,total_valid_lv, valid_lv_vals, valid_lv_vals1)
GribFileRecord *therec;
GribParamList *thevar;
GribRecordInqRecList *step;
int* n_it;
GIT **it_vals;
int* n_ft;
int **ft_vals;
int* total_valid_lv;
int** valid_lv_vals;
int** valid_lv_vals1;
#endif
{
	int i;
	GribRecordInqRecList *strt,*fnsh,*istep,*last;
	int n_its = 0;
	ITLIST header;
	ITLIST *the_end;
	int tmp_n_ft;
	int *tmp_ft_vals = NULL;
	int *tmp_lvs = NULL;
	int *tmp_lvs1 = NULL;
	int tmp_n_lvs = 0;
 	GIT current_it;


	the_end = &header;
	the_end->next = NULL;
	
	strt = istep = step;

	last = istep;
	current_it = strt->rec_inq->initial_time;
	while(istep->next != NULL) {
		if((istep->next->rec_inq->initial_time.year == current_it.year)
		   &&(istep->next->rec_inq->initial_time.days_from_jan1 == current_it.days_from_jan1)
		   &&(istep->next->rec_inq->initial_time.minute_of_day == current_it.minute_of_day)) {
			istep = istep->next;
			continue;
		}
		fnsh = istep;
		last = istep;
		istep = istep->next;
		fnsh->next = NULL;
		the_end->next = (ITLIST*)NclMalloc((unsigned)sizeof(ITLIST));
		the_end = the_end->next;
		the_end->it = current_it;
		the_end->next = NULL;
		the_end->ft_vals = NULL;
		the_end->lv_vals = NULL;
		the_end->lv_vals1 = NULL;
		the_end->thelist = GetFTList(therec,thevar,strt,&the_end->n_ft,&the_end->ft_vals,
					     &the_end->n_lv,&the_end->lv_vals,&the_end->lv_vals1);
		if(the_end->n_ft > 0) {
			if(tmp_ft_vals == NULL) {
				tmp_ft_vals = NclMalloc((unsigned)sizeof(int)*the_end->n_ft);
				tmp_n_ft = the_end->n_ft;
				memcpy((void*)tmp_ft_vals,the_end->ft_vals,the_end->n_ft*sizeof(int));
			} else {
				tmp_ft_vals = Merge(tmp_ft_vals,&tmp_n_ft,the_end->ft_vals,the_end->n_ft);
			}
		}

		if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 == -1)) {
			if(tmp_lvs == NULL) {
				tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
				tmp_n_lvs = the_end->n_lv;
				memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
			} else {
				tmp_lvs = Merge(tmp_lvs,&tmp_n_lvs,the_end->lv_vals,the_end->n_lv);
			}
		} else if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 != -1)){
/*
* Handle multiple value coordinate levels
*/
			if(tmp_lvs == NULL) {
				tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
				tmp_lvs1 = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
				tmp_n_lvs = the_end->n_lv;
				memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
				memcpy((void*)tmp_lvs1,the_end->lv_vals1,the_end->n_lv*sizeof(int));
			} else {
				Merge2(tmp_lvs,tmp_lvs1,&tmp_n_lvs,the_end->lv_vals,the_end->lv_vals1,the_end->n_lv,&tmp_lvs,&tmp_lvs1);
			}
		}
		strt = istep;
		current_it = strt->rec_inq->initial_time;
		n_its++;
	}
	the_end->next =(ITLIST*)NclMalloc((unsigned)sizeof(ITLIST));
	the_end = the_end->next;
	the_end->it = current_it;
	the_end->next = NULL;
	the_end->lv_vals = NULL;
	the_end->lv_vals1 = NULL;
	the_end->n_lv = 0;
	the_end->thelist = GetFTList(therec,thevar,strt,&the_end->n_ft,&the_end->ft_vals,
				     &the_end->n_lv,&the_end->lv_vals,&the_end->lv_vals1);
	if(the_end->n_ft > 0) {
		if(tmp_ft_vals == NULL) {
			tmp_ft_vals = NclMalloc((unsigned)sizeof(int)*the_end->n_ft);
			tmp_n_ft = the_end->n_ft;
			memcpy((void*)tmp_ft_vals,the_end->ft_vals,the_end->n_ft*sizeof(int));
		} else {
			tmp_ft_vals = Merge(tmp_ft_vals,&tmp_n_ft,the_end->ft_vals,the_end->n_ft);
		}
	}
	if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 == -1)){
		if(tmp_lvs != NULL) {
			tmp_lvs = Merge(tmp_lvs,&tmp_n_lvs,the_end->lv_vals,the_end->n_lv);
		} else {
			tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
			tmp_n_lvs = the_end->n_lv;
			memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
		}
	} else if((strt->rec_inq->level0 != -1)&&(strt->rec_inq->level1 != -1)){
/*
* Handle multiple value coordinate levels
*/
		if(tmp_lvs == NULL) {
			tmp_lvs = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
			tmp_lvs1 = (int*)NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
			tmp_n_lvs = the_end->n_lv;
			memcpy((void*)tmp_lvs,the_end->lv_vals,the_end->n_lv*sizeof(int));
			memcpy((void*)tmp_lvs1,the_end->lv_vals1,the_end->n_lv*sizeof(int));
		} else {
			Merge2(tmp_lvs,tmp_lvs1,&tmp_n_lvs,the_end->lv_vals,the_end->lv_vals1,the_end->n_lv,&tmp_lvs,&tmp_lvs1);
		}
	}
	n_its++;
	*n_it = n_its;
	*it_vals = NclMalloc((unsigned)sizeof(GIT)*n_its);
	the_end = header.next;
	for (i = 0; i < n_its; i++) {
		(*it_vals)[i] = the_end->it;
		the_end = the_end->next;
	}
	*ft_vals = tmp_ft_vals;
	*n_ft = tmp_n_ft;
	*total_valid_lv = tmp_n_lvs;
	*valid_lv_vals = tmp_lvs;
	*valid_lv_vals1 = tmp_lvs1;
	return(header.next);
}


int it_equal(GIT *it1,GIT* it2)
{
	if ((it1->year == it2->year) &&
	    (it1->days_from_jan1 == it2->days_from_jan1) &&
	    (it1->minute_of_day == it2->minute_of_day))
		return 1;
	return 0;
}

int ens_equal(ENS *ens1, ENS *ens2)
{
	if (ens1->type == 5 && ens1->prob_param) { 
		/* a probability product */
		if ((ens1->type == ens2->type) &&
		    (ens1->prob_type == ens2->prob_type) &&
		    (ens1->lower_prob == ens2->lower_prob) &&
		    (ens1->upper_prob == ens2->upper_prob)) {
			return 1;
		}
		else {
			return 0;
		}
	}
	else if ((ens1->prod_id == ens2->prod_id) &&
		 (ens1->type == ens2->type) &&
		 (ens1->id == ens2->id) &&
		 (ens1->extension_type == ens2->extension_type)) {
		return 1;
	}
	return 0;
}

static NhlErrorTypes _DetermineDimensionAndGridInfo
#if     NhlNeedProto
(GribFileRecord *therec, GribParamList* step)
#else
(therec,step)
GribFileRecord *therec;
GribParamList* step;
#endif
{
	GribRecordInqRecList *rstep,*strt,*fnsh,*free_rec;
	int n_ens = 0,i,j,k,m,icount = 0;
	ENS current_ens;
	ENSLIST  header;
	ENSLIST  *the_end,*free_ens;
	ITLIST  *itstep,*free_it;
	FTLIST  *ftstep,*free_ft;
	int *tmp_lv_vals= NULL;
	int *tmp_lv_vals1= NULL;
	int n_tmp_lv_vals = 0;
	int *tmp_ft_vals = NULL;
	int n_tmp_ft_vals = 0;
	GIT *tmp_it_vals = NULL;
	int n_tmp_it_vals = 0;
	NclQuark *it_vals_q = NULL;
	NclQuark *ens_vals_q = NULL;
	int *ens_indexes = NULL;
	int n_tmp_ens_vals = 0;
	ENS *tmp_ens_vals;
	ng_size_t total, tmp_dim_siz = 0;
	int doff;
	char *name;
	float *lprob, *uprob;
	NhlErrorTypes returnval = NhlNOERROR;

	doff = step->var_info.doff;
	the_end = &header;
	memset(&header,0,sizeof(ENSLIST));
	name = NrmQuarkToString(step->var_info.var_name_quark);

	if(step->n_entries > 1) {
		strt = rstep  = step->thelist;
		current_ens = rstep->rec_inq->ens;
		while(rstep->next != NULL) {
			if (ens_equal(&rstep->next->rec_inq->ens,&current_ens)) {
				rstep = rstep->next;
				continue;
			}
			current_ens = rstep->next->rec_inq->ens;
			fnsh = rstep;
			rstep = rstep->next;
			fnsh->next = NULL;
		
			the_end->next = (ENSLIST*)NclMalloc((unsigned)sizeof(ENSLIST));
			the_end = the_end->next;
			the_end->next = NULL;
			the_end->ens = strt->rec_inq->ens;
			the_end->ens_ix = n_ens;
			the_end->thelist = GetITList(therec,step,strt,&the_end->n_it,&the_end->it_vals,
						     &the_end->n_ft,&the_end->ft_vals,
						     &the_end->n_lv,&the_end->lv_vals,&the_end->lv_vals1);
			strt = rstep;
			n_ens++;
		}
		the_end->next = (ENSLIST*)NclMalloc((unsigned)sizeof(ENSLIST));
		the_end = the_end->next;
		the_end->next = NULL;
		the_end->ens = strt->rec_inq->ens;
		the_end->ens_ix = n_ens;
		the_end->thelist = GetITList(therec,step,strt,&the_end->n_it,&the_end->it_vals,
					     &the_end->n_ft,&the_end->ft_vals,
					     &the_end->n_lv,&the_end->lv_vals,&the_end->lv_vals1);
		n_ens++;
		the_end = header.next;

		n_tmp_ens_vals = n_ens;	
		tmp_ens_vals = (ENS*)NclMalloc(sizeof(ENS)*n_ens);
		i = 0;
		while(the_end != NULL) {
			tmp_ens_vals[i] = the_end->ens;
			if((the_end->n_lv > 0)&&(the_end->lv_vals1 == NULL) ) {
				if(tmp_lv_vals == NULL) {
					tmp_lv_vals = NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
					n_tmp_lv_vals = the_end->n_lv;
					memcpy((void*)tmp_lv_vals,the_end->lv_vals,the_end->n_lv*sizeof(int));
				} else 	{
					tmp_lv_vals  = Merge(tmp_lv_vals,&n_tmp_lv_vals,the_end->lv_vals,the_end->n_lv);
				}
			} else {
				if(tmp_lv_vals == NULL) {
					tmp_lv_vals = NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
					tmp_lv_vals1 = NclMalloc((unsigned)sizeof(int)*the_end->n_lv);
					n_tmp_lv_vals = the_end->n_lv;
					memcpy((void*)tmp_lv_vals,the_end->lv_vals,the_end->n_lv*sizeof(int));
					memcpy((void*)tmp_lv_vals1,the_end->lv_vals1,the_end->n_lv*sizeof(int));
				} else 	{
					Merge2(tmp_lv_vals,tmp_lv_vals1,&n_tmp_lv_vals,the_end->lv_vals,the_end->lv_vals1,the_end->n_lv,&tmp_lv_vals,&tmp_lv_vals1);
				}
			}
			if(the_end->n_ft > 0) {
				if(tmp_ft_vals == NULL) {
					tmp_ft_vals = NclMalloc((unsigned)sizeof(int)*the_end->n_ft);
					n_tmp_ft_vals = the_end->n_ft;
					memcpy((void*)tmp_ft_vals,the_end->ft_vals,the_end->n_ft*sizeof(int));
				} else {
					tmp_ft_vals = Merge(tmp_ft_vals,&n_tmp_ft_vals,the_end->ft_vals,the_end->n_ft);
				}
			}
			if(the_end->n_it > 0) {
				if(tmp_it_vals == NULL) {
					tmp_it_vals = (GIT *)NclMalloc((unsigned)sizeof(GIT)*the_end->n_it);
					n_tmp_it_vals = the_end->n_it;
					memcpy((void*)tmp_it_vals,the_end->it_vals,the_end->n_it*sizeof(GIT));

				} 
				else {
					tmp_it_vals = MergeIT(tmp_it_vals,&n_tmp_it_vals,the_end->it_vals,the_end->n_it);
				}
			}
			the_end = the_end->next;
/*
			fprintf(stdout,"%s\n",NrmQuarkToString(it_vals_q[i]));
*/
			i++;
		}
#if 0
		if(n_tmp_lv_vals > 0) {
			fprintf(stdout,"(");
			for(j = 0; j< n_tmp_lv_vals-1; j++) {	
				fprintf(stdout,"%d, ",tmp_lv_vals[j]);
			}

			fprintf(stdout,"%d)\n",tmp_lv_vals[j]);
		}
		if( n_tmp_ft_vals > 0) {
			fprintf(stdout,"(");
			for(j = 0; j< n_tmp_ft_vals-1; j++) {	
				fprintf(stdout,"%d, ",tmp_ft_vals[j]);
			}
			fprintf(stdout,"%d)\n",tmp_ft_vals[j]);
		}
#endif
		
	} else {
		n_tmp_ens_vals = 1;
		tmp_ens_vals = (ENS*)NclMalloc((unsigned)sizeof(ENS));
		memset(tmp_ens_vals,0,sizeof(ENS));
		header.next = (ENSLIST*)NclMalloc((unsigned)sizeof(ENSLIST));
		memset(header.next,0,sizeof(ENSLIST));
		memset(&header.next->ens,0,sizeof(ENS));
		the_end = header.next;
		the_end->thelist = GetITList(therec,step,step->thelist,&n_tmp_it_vals,&tmp_it_vals,
					     &n_tmp_ft_vals,&tmp_ft_vals,
					     &n_tmp_lv_vals,&tmp_lv_vals,&tmp_lv_vals1);
/*
		fprintf(stdout,"%d/%d/%d\t(%d:%d)-%d,%d\t%d,%d\ttoff=%d\t%d,%d,%d\n",
			(int)step->thelist->rec_inq->pds[13],
			(int)step->thelist->rec_inq->pds[14],
			(int)step->thelist->rec_inq->pds[12],
			(int)step->thelist->rec_inq->pds[15],
			(int)step->thelist->rec_inq->pds[16],
			(int)step->thelist->rec_inq->pds[18],
			(int)step->thelist->rec_inq->pds[19],
			(int)step->thelist->rec_inq->pds[17],
			(int)step->thelist->rec_inq->pds[20],
			step->thelist->rec_inq->time_offset,
			(int)step->thelist->rec_inq->pds[9],
			step->thelist->rec_inq->level0,
			step->thelist->rec_inq->level1);
*/
	}

	i = 0;
	step->var_info.num_dimensions = 0;
	if (step->prob_param && n_tmp_ens_vals > 0) {
		float minlp, maxlp, minup, maxup;
		lprob = NclMalloc(n_tmp_ens_vals * sizeof(float));
		uprob = NclMalloc(n_tmp_ens_vals * sizeof(float));
		minlp = maxlp = tmp_ens_vals[0].lower_prob;
		minup = maxup = tmp_ens_vals[0].upper_prob;
		for (j = 0; j < n_tmp_ens_vals; j++) {
			lprob[j] = tmp_ens_vals[j].lower_prob * 1e-6;
			uprob[j] = tmp_ens_vals[j].upper_prob * 1e-6;
			/*
			if (lprob[j] < minlp) minlp = lprob[j];
			if (lprob[j] > maxlp) maxlp = lprob[j];
			if (uprob[j] < minup) minup = uprob[j];
			if (uprob[j] > maxup) maxup = uprob[j];
			*/
		}
		/*
		const_lp = maxlp == minlp;
		const_up = maxup == minup;
		*/
		step->lower_probs = NULL;
		step->upper_probs = NULL;
		step->probability = NULL;
		if (tmp_ens_vals[0].prob_type == 1) {
			tmp_dim_siz = (ng_size_t) n_tmp_ens_vals;
			step->probability = (NclOneDValCoordData)_NclCreateVal(
				NULL,
				NULL,
				Ncl_OneDValCoordData,
				0,
				(void*)lprob,
				NULL,
				1,
/*				(void*)&n_tmp_ens_vals,*/
                &tmp_dim_siz,
				TEMPORARY,
				NULL,
				nclTypefloatClass);
			NclFree(uprob);
		}
		else if (tmp_ens_vals[0].prob_type == 2) {
			tmp_dim_siz = (ng_size_t) n_tmp_ens_vals;
			step->probability = (NclOneDValCoordData)_NclCreateVal(
				NULL,
				NULL,
				Ncl_OneDValCoordData,
				0,
				(void*)uprob,
				NULL,
				1,
/*				(void*)&n_tmp_ens_vals,*/
                &tmp_dim_siz,
				TEMPORARY,
				NULL,
				nclTypefloatClass);
			NclFree(lprob);
		}
		else {
			tmp_dim_siz = (ng_size_t) n_tmp_ens_vals;
			step->lower_probs = (NclMultiDValData)_NclCreateVal(
				NULL,
				NULL,
				Ncl_MultiDValData,
				0,
				(void*)lprob,
				NULL,
				1,
/*				(void*)&n_tmp_ens_vals,*/
                &tmp_dim_siz,
				TEMPORARY,
				NULL,
				nclTypefloatClass);
			step->upper_probs = (NclMultiDValData)_NclCreateVal(
				NULL,
				NULL,
				Ncl_MultiDValData,
				0,
				(void*)uprob,
				NULL,
				1,
/*				(void*)&n_tmp_ens_vals,*/
                &tmp_dim_siz,
				TEMPORARY,
				NULL,
				nclTypefloatClass);
		}
	}
	else if (n_tmp_ens_vals > 0) {
		ens_vals_q = (NclQuark *) NclMalloc(sizeof(NclQuark) * n_tmp_ens_vals);
		ens_indexes = (int *) NclMalloc(sizeof(int) * n_tmp_ens_vals);
		for (j = 0; j < n_tmp_ens_vals; j++) {
			ens_vals_q[j] = GetEnsQuark(&(tmp_ens_vals[j]));
			ens_indexes[j] = j;
		}
		tmp_dim_siz = (ng_size_t) n_tmp_ens_vals;
		step->ensemble = (NclMultiDValData)_NclCreateVal(
					NULL,
					NULL,
					Ncl_MultiDValData,
					0,
					(void*)ens_vals_q,
					NULL,
					1,
/*					(void*)&n_tmp_ens_vals,*/
                    &tmp_dim_siz,
					TEMPORARY,
					NULL,
					nclTypestringClass);
		step->ens_indexes = (NclOneDValCoordData)_NclCreateVal(
					NULL,
					NULL,
					Ncl_OneDValCoordData,
					0,
					(void*)ens_indexes,
					NULL,
					1,
/*					(void*)&n_tmp_ens_vals,*/
                    &tmp_dim_siz,
					TEMPORARY,
					NULL,
					nclTypeintClass);
	}
	if(n_tmp_ens_vals > 1 || 
	   (n_tmp_ens_vals > 0 && 
	    step->thelist->rec_inq->is_ensemble && 
	    (therec->single_dims & GRIB_Ensemble_Dims))) {
		step->var_info.dim_sizes[i] = n_tmp_ens_vals;
		step->ensemble_isatt = 0;
		i++;
	} else if(n_tmp_ens_vals == 1) {
		step->ensemble_isatt = 1;
	} else {
		step->ensemble_isatt = 0;
	}
	NclFree(tmp_ens_vals);
	if (n_tmp_it_vals > 0) {
		it_vals_q = (NclQuark *) NclMalloc(sizeof(NclQuark) * n_tmp_it_vals);
		for (j = 0; j < n_tmp_it_vals; j++)
			it_vals_q[j] = GetItQuark(&(tmp_it_vals[j]));
	}
	if(n_tmp_it_vals > 1 || (n_tmp_it_vals > 0 && (therec->single_dims & GRIB_Initial_Time_Dims))) {
		step->var_info.dim_sizes[i] = (ng_size_t) n_tmp_it_vals;
		tmp_dim_siz = (ng_size_t) n_tmp_it_vals;
		step->yymmddhh = (NclOneDValCoordData)_NclCreateVal(
					NULL,
					NULL,
					Ncl_OneDValCoordData,
					0,
					(void*)it_vals_q,
					NULL,
					1,
/*					(void*)&n_tmp_it_vals,*/
                    &tmp_dim_siz,
					TEMPORARY,
					NULL,
					nclTypestringClass);
		step->it_vals = tmp_it_vals;
			
		step->yymmddhh_isatt = 0;
		i++;
	} else if(n_tmp_it_vals == 1) {
		step->yymmddhh_isatt = 1;
		tmp_dim_siz = (ng_size_t) n_tmp_it_vals;
		step->yymmddhh = (NclOneDValCoordData)_NclCreateVal(
					NULL,
					NULL,
					Ncl_OneDValCoordData,
					0,
					(void*)it_vals_q,
					NULL,
					1,
/*					(void*)&n_tmp_it_vals,*/
					&tmp_dim_siz,
					TEMPORARY,
					NULL,
					nclTypestringClass);
		step->it_vals = tmp_it_vals;
	} else {
		step->yymmddhh_isatt = 0;
		step->yymmddhh = NULL;
		step->it_vals = NULL;
/*
		fprintf(stdout,"n_it: %d\n",n_tmp_it_vals);
*/
	}
	if(n_tmp_ft_vals > 1 || (n_tmp_ft_vals > 0 && (therec->single_dims & GRIB_Forecast_Time_Dims))) {
		step->var_info.dim_sizes[i] = (ng_size_t) n_tmp_ft_vals;
		tmp_dim_siz = (ng_size_t) n_tmp_ft_vals;
		step->forecast_time = (NclOneDValCoordData)_NclCreateVal(
					NULL,
					NULL,
					Ncl_OneDValCoordData,
					0,
					(void*)tmp_ft_vals,
					NULL,
					1,
/*					(void*)&n_tmp_ft_vals,*/
                    &tmp_dim_siz,
					TEMPORARY,
					NULL,
					nclTypeintClass);
		step->forecast_time_isatt = 0;
		i++;
	} else if(n_tmp_ft_vals == 1) {
		tmp_dim_siz = (ng_size_t) n_tmp_ft_vals;
		step->forecast_time = (NclOneDValCoordData)_NclCreateVal(
					NULL,
					NULL,
					Ncl_OneDValCoordData,
					0,
					(void*)tmp_ft_vals,
					NULL,
					1,
/*					(void*)&n_tmp_ft_vals,*/
                    &tmp_dim_siz,
					TEMPORARY,
					NULL,
					nclTypeintClass);
		step->forecast_time_isatt = 1;
	} else {
		step->forecast_time_isatt = 0;
/*
		fprintf(stdout,"n_ft: %d\n",n_tmp_ft_vals);
*/
	}
	if((tmp_lv_vals != NULL)&&(tmp_lv_vals1 == NULL)) {
		if(n_tmp_lv_vals > 1 || (n_tmp_lv_vals > 0 && (therec->single_dims & GRIB_Level_Dims))) {
			step->var_info.dim_sizes[i] = (ng_size_t) n_tmp_lv_vals;
			tmp_dim_siz = (ng_size_t) n_tmp_lv_vals;
			step->levels = (NclOneDValCoordData)_NclCreateVal(
						NULL,
						NULL,
						Ncl_OneDValCoordData,
						0,
						(void*)tmp_lv_vals,
						NULL,
						1,
/*						(void*)&n_tmp_lv_vals,*/
                        &tmp_dim_siz,
						TEMPORARY,
						NULL,
						nclTypeintClass);
			step->levels0 = NULL;
			step->levels1 = NULL;
			i++;
				step->levels_isatt = 0;
		} else if (n_tmp_lv_vals == 1) {
			step->levels_isatt = 1;
			tmp_dim_siz = (ng_size_t) n_tmp_lv_vals;
			step->levels = (NclOneDValCoordData)_NclCreateVal(
						NULL,
						NULL,
						Ncl_OneDValCoordData,
						0,
						(void*)tmp_lv_vals,
						NULL,
						1,
/*						(void*)&n_tmp_lv_vals,*/
                        &tmp_dim_siz,
						TEMPORARY,
						NULL,
						nclTypeintClass);
			step->levels0 = NULL;
			step->levels1 = NULL;
		} else {
			step->levels_isatt = 0;
/*
			fprintf(stdout,"n_lv: %d\n",n_tmp_lv_vals);
*/
		}
	} else if((tmp_lv_vals != NULL)&&(tmp_lv_vals1 != NULL)) { 
		if(n_tmp_lv_vals > 1 || (n_tmp_lv_vals > 0 && (therec->single_dims & GRIB_Level_Dims))) {
			step->var_info.dim_sizes[i] = (ng_size_t) n_tmp_lv_vals;
			tmp_dim_siz = (ng_size_t) n_tmp_lv_vals;
			step->levels = NULL;
			step->levels0 = (NclMultiDValData)_NclCreateVal(
						NULL,
						NULL,
						Ncl_MultiDValData,
						0,
						(void*)tmp_lv_vals,
						NULL,
						1,
/*						(void*)&n_tmp_lv_vals,*/
                        &tmp_dim_siz,
						TEMPORARY,
						NULL,
						nclTypeintClass);
			step->levels1 = (NclMultiDValData)_NclCreateVal(
						NULL,
						NULL,
						Ncl_MultiDValData,
						0,
						(void*)tmp_lv_vals1,
						NULL,
						1,
/*						(void*)&n_tmp_lv_vals,*/
                        &tmp_dim_siz,
						TEMPORARY,
						NULL,
						nclTypeintClass);
			step->levels_has_two = 1;
			i++;
				step->levels_isatt = 0;
		} else if (n_tmp_lv_vals == 1) {
			step->levels_isatt = 1;
			step->levels = NULL;
			tmp_dim_siz = (ng_size_t) n_tmp_lv_vals;
			step->levels0 = (NclMultiDValData)_NclCreateVal(
						NULL,
						NULL,
						Ncl_MultiDValData,
						0,
						(void*)tmp_lv_vals,
						NULL,
						1,
/*						(void*)&n_tmp_lv_vals,*/
                        &tmp_dim_siz,
						TEMPORARY,
						NULL,
						nclTypeintClass);
			step->levels1 = (NclMultiDValData)_NclCreateVal(
						NULL,
						NULL,
						Ncl_MultiDValData,
						0,
						(void*)tmp_lv_vals1,
						NULL,
						1,
/*						(void*)&n_tmp_lv_vals,*/
                        &tmp_dim_siz,
						TEMPORARY,
						NULL,
						nclTypeintClass);
			step->levels_has_two = 1;
		} else {
			step->levels_isatt = 0;
/*
			fprintf(stdout,"n_lv: %d\n",n_tmp_lv_vals);
*/
		}
	} else {
		step->levels_isatt = 0;
	}
	step->var_info.num_dimensions = i + (doff+1);
	for (i = 0; i < step->var_info.num_dimensions; i++) {
		/* initialize the file dim number to something out of range */
		step->var_info.file_dim_num[i] = -1;
	}
/*
* Now call grid code to get coordinates
*/
/*
* Now build single array of GribRecordInqRecList*'s
*/
	if( step->var_info.num_dimensions - (doff+1) <= 0) {
		if(header.next != NULL) {
			free_ens = header.next;
			if(free_ens->lv_vals != NULL) 
				NclFree(free_ens->lv_vals);
			if(free_ens->lv_vals1 != NULL) 
				NclFree(free_ens->lv_vals1);
			if(free_ens->ft_vals != NULL) 
				NclFree(free_ens->ft_vals);
			if(free_ens->it_vals != NULL) 
				NclFree(free_ens->it_vals);
			if(free_ens->thelist != NULL) {
				if(free_ens->thelist->lv_vals != NULL)
					NclFree(free_ens->thelist->lv_vals);
				if(free_ens->thelist->lv_vals1 != NULL)
					NclFree(free_ens->thelist->lv_vals1);
				if(free_ens->thelist->ft_vals != NULL)
					NclFree(free_ens->thelist->ft_vals);
				if (free_ens->thelist->thelist != NULL) {
					if (free_ens->thelist->thelist->lv_vals != NULL)
						NclFree(free_ens->thelist->thelist->lv_vals);
					if (free_ens->thelist->thelist->lv_vals1 != NULL)
						NclFree(free_ens->thelist->thelist->lv_vals1);
					NclFree(free_ens->thelist->thelist);
				}
				NclFree(free_ens->thelist);
			}
			NclFree(free_ens);
		}
		return( returnval);
	}

	total = 1;
	for(i = 0; i < step->var_info.num_dimensions - (doff +1); i++) {
		total *= step->var_info.dim_sizes[i];
	}
	strt = (GribRecordInqRecList*)NclMalloc((unsigned)sizeof(GribRecordInqRecList)*total);
	for (i = 0; i < total; i++) {
		strt[i].rec_inq = (GribRecordInqRec*)10;
		strt[i].next = NULL;
	}
	the_end = header.next;
	i = 0;
	icount = 0;

#define PRINT_MISSING(ens_ix,it_ix,ft_ix,lv_ix) \
	sprintf(buf,"%s->%s is missing",NrmQuarkToString(therec->file_path_q),name); \
	if (n_tmp_ens_vals > 1) \
		sprintf(&(buf[strlen(buf)])," ens: %d",ens_ix); \
	if (n_tmp_it_vals > 1) \
		sprintf(&(buf[strlen(buf)])," it: %s",NrmQuarkToString(it_vals_q[it_ix])); \
	if (n_tmp_ft_vals > 1) \
		sprintf(&(buf[strlen(buf)])," ft: %d",tmp_ft_vals[ft_ix]); \
	if (n_tmp_lv_vals > 1) { \
		if (! step->levels_has_two) {	\
			sprintf(&(buf[strlen(buf)])," lv: %d",tmp_lv_vals[lv_ix]); \
		} \
		else {	\
			sprintf(&(buf[strlen(buf)])," lv: (%d, %d)",tmp_lv_vals[lv_ix],tmp_lv_vals1[lv_ix]); \
		} \
	} \
	NhlPError(NhlWARNING,NhlEUNKNOWN,buf)
			
	while(the_end != NULL) {
		char buf[256];
		itstep = the_end->thelist;
		j = 0;
		while(itstep != NULL) {
			ftstep = itstep->thelist;
			if ((tmp_it_vals != NULL) && (! it_equal(&itstep->it,&(tmp_it_vals[j])))) {
				for (k = 0; k < n_tmp_ft_vals; k++) {
					for( m = 0 /* i already set */; m < n_tmp_lv_vals; i++,m++) {
						strt[i].rec_inq = NULL;
						PRINT_MISSING(the_end->ens_ix,j,k,m);
					}
				}
				j++;
				continue;
			}
			k = 0;
			while (ftstep != NULL) {
				rstep = ftstep->thelist;
				if((tmp_ft_vals != NULL)&&(ftstep->ft != tmp_ft_vals[k])){
					for( m = 0 /* i already set */; m < n_tmp_lv_vals; i++,m++) {
						strt[i].rec_inq = NULL;
						PRINT_MISSING(the_end->ens_ix,j,k,m);
					}
					k++;
					continue;
				}
				m = 0;
				if(!step->levels_has_two) {
					while(rstep != NULL) {
						if((tmp_lv_vals == NULL) ||(rstep->rec_inq->level0 == tmp_lv_vals[m])) {
							strt[i].rec_inq = rstep->rec_inq;	
							icount +=1;
							free_rec = rstep;
							rstep = rstep->next;
							NclFree(free_rec);
							m++;
						} else {
							strt[i].rec_inq = NULL;
							PRINT_MISSING(the_end->ens_ix,j,k,m);
							m++;
						}
						i++;
					}
					if((rstep == NULL)&&(m < n_tmp_lv_vals)) {
						for( ;m < n_tmp_lv_vals; m++) {
							strt[i].rec_inq = NULL;
							PRINT_MISSING(the_end->ens_ix,j,k,m);
							i++;
						}
					}
				} else {
					while(rstep != NULL) {
						if((rstep->rec_inq->level0 == tmp_lv_vals[m])
						   &&(rstep->rec_inq->level1 == tmp_lv_vals1[m])) {
							strt[i].rec_inq = rstep->rec_inq;	
							icount +=1;
							free_rec = rstep;
							rstep = rstep->next;
							NclFree(free_rec);
							m++;
						} else {
							strt[i].rec_inq = NULL;
							PRINT_MISSING(the_end->ens_ix,j,k,m);
							m++;
						}
						i++;
					}
					if((rstep == NULL)&&(m < n_tmp_lv_vals)) {
						for( ;m < n_tmp_lv_vals; m++) {
							strt[i].rec_inq = NULL;
							PRINT_MISSING(the_end->ens_ix,j,k,m);
							i++;
						}
					}
				}
				free_ft = ftstep;
				ftstep = ftstep->next;
				if(free_ft->lv_vals != NULL) 
					NclFree(free_ft->lv_vals);
				if(free_ft->lv_vals1 != NULL) 
					NclFree(free_ft->lv_vals1);
				NclFree(free_ft);
				k++;
			}
			while(k < n_tmp_ft_vals) {
				for( m = 0 /* i already set */; m < n_tmp_lv_vals; i++,m++) {
					strt[i].rec_inq = NULL;
					PRINT_MISSING(the_end->ens_ix,j,k,m);
				}
				k++;
			}
			free_it = itstep;
			itstep = itstep->next;
			if (free_it->lv_vals != NULL)
				NclFree(free_it->lv_vals);
			if(free_it->lv_vals1 != NULL) 
				NclFree(free_it->lv_vals1);
			if(free_it->ft_vals != NULL) 
				NclFree(free_it->ft_vals);
			NclFree(free_it);
			j++;
		}
		while (j < n_tmp_it_vals) {
			for (k = 0; k < n_tmp_ft_vals; k++) {
				for( m = 0 /* i already set */; m < n_tmp_lv_vals; i++,m++) {
					strt[i].rec_inq = NULL;
					PRINT_MISSING(the_end->ens_ix,j,k,m);
				}
			}
			j++;
		}
		free_ens = the_end;
		the_end = the_end->next;
		if(free_ens->lv_vals != NULL) 
			NclFree(free_ens->lv_vals);
		if(free_ens->lv_vals1 != NULL) 
			NclFree(free_ens->lv_vals1);
		if(free_ens->ft_vals != NULL) 
			NclFree(free_ens->ft_vals);
		if(free_ens->it_vals != NULL) 
			NclFree(free_ens->it_vals);
		NclFree(free_ens);
	}
	while(i<total) strt[i++].rec_inq = NULL;
	step->thelist = strt;
	step->n_entries = total;
	
	return(returnval);
}


unsigned int UnsignedCnvtToDecimal
#if	NhlNeedProto
(int n_bytes,unsigned char *val)
#else
(n_bytes,val)
int n_bytes;
unsigned char *val;
#endif
{
	CVT tmp;
	int i = 0;
	
	tmp.c[0] = (char)0;
	tmp.c[1] = (char)0;
	tmp.c[2] = (char)0;
	tmp.c[3] = (char)0;
#ifndef ByteSwapped
	if(n_bytes == 4) {
		tmp.c[0] = val[i];
		i++;
	}
	if(n_bytes >= 3) {
		tmp.c[1] = val[i];
		i++;
	}
	if(n_bytes >= 2) {
		tmp.c[2] = val[i];
		i++;
	}
	if(n_bytes >= 1) {
		tmp.c[3] = val[i];
		i++;
	}
#else
	if(n_bytes == 4) {
		tmp.c[3] = val[i];
		i++;
	}
	if(n_bytes >= 3) {
		tmp.c[2] = val[i];
		i++;
	}
	if(n_bytes >= 2) {
		tmp.c[1] = val[i];
		i++;
	}
	if(n_bytes >= 1) {
		tmp.c[0] = val[i];
		i++;
	}
#endif
	return(tmp.value);
}
int CnvtToDecimal
#if	NhlNeedProto
(int n_bytes,unsigned char *val)
#else
(n_bytes,val)
int n_bytes;
unsigned char *val;
#endif
{
	CVT tmp;
	int i = 0;
	
	tmp.c[0] = (char)0;
	tmp.c[1] = (char)0;
	tmp.c[2] = (char)0;
	tmp.c[3] = (char)0;

#ifndef ByteSwapped
	if(n_bytes == 4) {
		tmp.c[0] = val[i];
		i++;
	}
	if(n_bytes >= 3) {
		tmp.c[1] = val[i];
		i++;
	}
	if(n_bytes >= 2) {
		tmp.c[2] = val[i];
		i++;
	}
	if(n_bytes >= 1) {
		tmp.c[3] = val[i];
		i++;
	}
#else
	if(n_bytes == 4) {
		tmp.c[3] = val[i];
		i++;
	}
	if(n_bytes >= 3) {
		tmp.c[2] = val[i];
		i++;
	}
	if(n_bytes >= 2) {
		tmp.c[1] = val[i];
		i++;
	}
	if(n_bytes >= 1) {
		tmp.c[0] = val[i];
		i++;
	}
#endif
	return(tmp.ivalue);
}

	
int GetNextGribOffset
#if NhlNeedProto
(int gribfile,off_t *offset, unsigned int *totalsize, off_t startoff, off_t *nextoff,int* version)
#else
(gribfile, offset, totalsize, startoff, nextoff,version)
int gribfile;
off_t *offset;
unsigned int *totalsize;
off_t startoff;
off_t *nextoff;
int *version;
#endif
{
	int j,ret1,ret4;
	unsigned char *is; /* pointer to indicator section */
	char test[10];
	unsigned char nd[10];
	unsigned int size;
	off_t t;
	int len;
	int tries = 0;
	off_t off;
#ifdef GRIBRECDUMP
	static int fd_out;
	static int count = 0;
	void *tmp;
#endif

#define LEN_HEADER_PDS (28+8)

	ret1 = 0;
	ret4 = 0;

	test[4] = '\0';

	off = startoff;
	while(1) {
		tries++;
		if (tries > 100) {
			NhlPError(NhlFATAL,NhlEUNKNOWN,"100 blocks read without finding start of GRIB record -- is this a GRIB file?");
			*totalsize = 0;
			return(GRIBEOF);
		}	 
		/* jump into GRIB file, read vbuflen bytes at a time */
		lseek(gribfile,off,SEEK_SET);
		ret1 = read(gribfile,(void*)vbuf,vbuflen);
		if(ret1 > 0) {
			len = ret1 - LEN_HEADER_PDS;
			for (j = 0; j < len; j++) {
				/* look for "GRIB" indicator */
				if (vbuf[j] != 'G') 
					continue;
				if (! (vbuf[j+1] == 'R' && vbuf[j+2] == 'I' && vbuf[j+3] == 'B'))
					continue;

				*version = vbuf[j+7];
/*
				fprintf(stdout,"found GRIB\n");
*/
				if(*version == 1){
					is = &(vbuf[j]);
/*
					fprintf(stdout,"found GRIB version 1\n");
*/
					*offset = off + j;
					size = UnsignedCnvtToDecimal(3,&(is[4]));
#ifdef GRIBRECDUMP
					if(count == 0) {
						fd_out = open("./tmp.out.grb",(O_CREAT|O_RDWR));
					}
					if(count < 3) {
						tmp = NclMalloc(sizeof(char)*size);
				
						lseek(gribfile,*offset,SEEK_SET);	
						read(gribfile,tmp,size);
						write(fd_out,tmp,size);
						count++;
					}
					if(count == 3) {
						close(fd_out);
						count++;
					}
#endif
					lseek(gribfile,*offset+size - 4,SEEK_SET);
					ret4 = read(gribfile,(void*)nd,4);
					if(ret4 < 4) {
						NhlPError(NhlFATAL,NhlEUNKNOWN,"Premature end-of-file, file appears to be truncated");
						*totalsize = 0;
						return(GRIBEOF);
					}	 
					test[0] = nd[0];
					test[1] = nd[1];
					test[2] = nd[2];
					test[3] = nd[3];
					test[4] = '\0';
					*nextoff = *offset + size;
					if(!strncmp("7777",test,4)) {
/*
					fprintf(stdout,"found 7777\n");
*/
						*totalsize = size;
						return(GRIBOK);
					} else {
						*totalsize = size;
						return(GRIBERROR);
					}
				}
				else if(*version == 0){
					int has_bms,has_gds;
					int pdssize, gdssize = 0,bmssize,bdssize;
					unsigned char *pds;
					int k;
					int tsize;

					is = &(vbuf[j]);
					pds = is + 4;
					*offset = off + j;
/*
  					fprintf(stdout,"found GRIB version 0 at offset %d\n",*offset);
*/
					has_gds = (pds[7] & (char)0200) ? 1 : 0;
					has_bms = (pds[7] & (char)0100) ? 1 : 0;
					size = 4;
					t = *offset + size;
					lseek(gribfile,t,SEEK_SET);
					ret4 = read(gribfile,(void*)nd,4);
					pdssize = UnsignedCnvtToDecimal(3,nd);
					size += pdssize;
					t = *offset + size;
					if (has_gds) {
						lseek(gribfile,t,SEEK_SET);
						ret4 = read(gribfile,(void*)nd,4);
						gdssize = UnsignedCnvtToDecimal(3,nd);
						size += gdssize;
						t = *offset + size;
					}
					if (has_bms) {
						lseek(gribfile,t,SEEK_SET);
						ret4 = read(gribfile,(void*)nd,4);
						bmssize = UnsignedCnvtToDecimal(3,nd);
						size += bmssize;
						t = *offset + size;
					}
					lseek(gribfile,t,SEEK_SET);
					ret4 = read(gribfile,(void*)nd,4);
					bdssize = UnsignedCnvtToDecimal(3,nd);
					size += bdssize;
					tsize = size;
					if (gdssize > 32 || pdssize > 24) { /* be suspiscious of this record */
						tsize = 4;
					}
					while (1) {
						t = *offset + tsize;
						lseek(gribfile,t,SEEK_SET);
						ret4 = read(gribfile,(void*)vbuf,vbuflen);
						if (ret4 < 4) {
							NhlPError(NhlFATAL,NhlEUNKNOWN,"Premature end-of-file, file appears to be truncated");
							break;
						}
						for (k = 0; k < vbuflen-4; k++) {
						     if (! vbuf[k] == '7')
							     continue;
						     if(strncmp("7777",(char*)&vbuf[k],4))
							     continue;
						     tsize += (k + 4);
						     *nextoff = *offset + tsize ;
#if 0
						     fprintf(stdout,"found 7777 at offset %d size %d\n",*nextoff - 4,size);
#endif
						     *totalsize = tsize;
						     if (tsize < size + 4) {
#if 0
							     printf("bad record, tsize %d size %d************************\n",tsize,size);
#endif
							     return (GRIBERROR);
						     }
						     return(GRIBOK);
						}
						tsize += vbuflen-8; /* make sure we don't lose the beginning of the end indicator */
					}

#if 0
/
* This gives me the pds size
*/
					size = 4;
					while(1) {
						t = *offset + size;
						lseek(gribfile,t,SEEK_SET);
						ret4 = read(gribfile,(void*)nd,4);
						if (ret4 < 4) {
							NhlPError(NhlFATAL,NhlEUNKNOWN,"Premature end-of-file, file appears to be truncated");
							break;
						}
						test[0] = nd[0];
						test[1] = nd[1];
						test[2] = nd[2];
						test[3] = nd[3];
						test[4] = '\0';
						if(!strncmp("7777",test,4)) {
							size += ret4;
							*nextoff = *offset + size ;
							*totalsize = size;
							return(GRIBOK);
						} else {
							size += 4;
						}
						
					}
					*totalsize = size;
					return(GRIBEOF);
#endif
				}
			}
			off += (vbuflen - LEN_HEADER_PDS);
		} else {
			*totalsize = 0;
			return(GRIBEOF);
		}
	}
}

static int _GetLevels
#if NhlNeedProto
(int *l0,int *l1,int indicator,unsigned char* lv)
#else
(l0,l1,indicator,lv)
int *l0;
int *l1;
int indicator;
unsigned char *lv;
#endif
{
	if(indicator  < 100) {
		*l0 = -1;
		*l1 = -1;
	}
	switch(indicator) {
	case 100:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		return(1);
	case 101:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 102:
		*l0 = -1;
		*l1 = -1;
		return(1);
	case 103:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 104:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 105:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 106:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 107:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		*l1 = -1;
		break;
	case 108:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 109:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 110:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 111:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 112:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 113:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 114:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 115:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 116:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 117:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 119:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 120:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 121:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 125:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 128:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 141:
		*l0 = (int)lv[0];
		*l1 = (int)lv[1];
		break;
	case 160:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 200:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 201:
		*l0 = CnvtToDecimal(2,lv);
		*l1 = -1;
		break;
	case 1:
		*l0 = -1;
		*l1 = -1;
		return(1);
	default: 
		*l0 = -1;
		*l1 = -1;
	}
	return(0);
}

static void _SetCommonTimeUnit
#if	NhlNeedProto
(GribParamList *node, GribRecordInqRec* grib_rec)
#else
(node,grib_rec)
GribParamList *node;
GribRecordInqRec* grib_rec;
#endif
{
	int cix, nix;
	static int month_ix = 7;
	static NrmQuark var_name_q = NrmNULLQUARK;
	/* 
	 * These are the codes in ON388 - Table 4 - for time units arranged in order from 
	 * short to long duration. 
	 */

	for (cix = 0; cix < NhlNumber(Unit_Code_Order); cix++) {
		if (node->time_unit_indicator == Unit_Code_Order[cix])
			break;
	}
	for (nix = 0; nix < NhlNumber(Unit_Code_Order); nix++) {
		if ((int)grib_rec->pds[17] == Unit_Code_Order[nix])
			break;
	}
	if (nix >= NhlNumber(Unit_Code_Order)) {
		NhlPError(NhlWARNING,NhlEUNKNOWN,
			  "NclGRIB: Unsupported time unit found for parameter (%s), continuing anyways.",
			  NrmQuarkToString(grib_rec->var_name_q));
	}
	else if (cix >= NhlNumber(Unit_Code_Order)) { 
		/* current time units are unsupported so use the new unit */
		node->time_unit_indicator = (int)grib_rec->pds[17];
	}
	else if (Unit_Code_Order[nix] < Unit_Code_Order[cix]) { 
		/* choose the shortest duration as the common unit */
		node->time_unit_indicator = (int)grib_rec->pds[17];
	}
	if (nix >= month_ix && var_name_q != grib_rec->var_name_q) {
		NhlPError(NhlWARNING,NhlEUNKNOWN,
			  "NclGRIB: Variable time unit codes representing time durations of a month or more in variable (%s): requires approximation to convert to common unit",
			  NrmQuarkToString(grib_rec->var_name_q));
		var_name_q = grib_rec->var_name_q;
	}
		
	/* Set the variable_time_unit flag */
	node->variable_time_unit = True;

	return;
}
static int _GetTimeOffset 
#if	NhlNeedProto
( int time_indicator, unsigned char *offset)
#else
( time_indicator, offset)
int time_indicator;
unsigned char *offset;
#endif
{
	switch(time_indicator) {
	case 0: /* reference time + P1 */
	case 1: /* reference time + P1 */
		return((int)offset[0]);
	case 2: /* reference time + P1 < t < reference time + P2 */
	case 3: /* Average from reference time + P1 to reference time + P2 */
	case 4: /* Accumulation from reference time + P1 to reference time + P2 */
	case 5: /* Difference from reference time + P1 to reference time + P2 */
		return((int)offset[1]);
	case 6: /* Average from reference time - P1 to reference time - P2 */
		return(-(int)offset[1]);
	case 7: /* Average from reference time - P1 to reference time + P2 */
		return((int)offset[1]);
	case 10:/* P1 occupies both bytes */
		return(UnsignedCnvtToDecimal(2,offset));
	case 51:
	case 113:
	case 114:
	case 115:
	case 116:
	case 118:
	case 123:
	case 124:
		return 0;
	case 117:
		return((int)offset[0]);
	default:
		NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Unknown or unsupported time range indicator detected, continuing");
		return(-1);
	}
}

/*
 * This is like _GetTimeOffset except that it converts the value to a common unit
 * for variables with variable time units
 */

static int _GetConvertedTimeOffset 
#if	NhlNeedProto
(int common_time_unit, int time_unit, int time_indicator, unsigned char *offset)
#else
(common_time_unit, time_unit, time_indicator, offset)
int common_time_unit;
int time_unit;
int time_indicator;
unsigned char *offset;
#endif
{
	int cix,tix, ret_val;
	double c_factor = 1.0;


	if (common_time_unit != time_unit) {
		for (cix = 0; cix < NhlNumber(Unit_Code_Order); cix++) {
			if (common_time_unit == Unit_Code_Order[cix])
				break;
		}
		for (tix = 0; tix < NhlNumber(Unit_Code_Order); tix++) {
			if (time_unit == Unit_Code_Order[tix])
				break;
		}
		/* this condition must be met in order to do a valid conversion */
		if (cix < NhlNumber(Unit_Code_Order) && tix < NhlNumber(Unit_Code_Order)) { 
			c_factor = Unit_Convert[tix] / Unit_Convert[cix];
		}
	}
	switch(time_indicator) {
	case 0: /* reference time + P1 */
	case 1: /* reference time + P1 */
		ret_val = (int)offset[0];
		break;
	case 2: /* reference time + P1 < t < reference time + P2 */
	case 3: /* Average from reference time + P1 to reference time + P2 */
	case 4: /* Accumulation from reference time + P1 to reference time + P2 */
	case 5: /* Difference from reference time + P1 to reference time + P2 */
		ret_val = (int)offset[1];
		break;
	case 6: /* Average from reference time - P1 to reference time - P2 */
		ret_val = -(int)offset[1];
		break;
	case 7: /* Average from reference time - P1 to reference time + P2 */
		ret_val = (int)offset[1];
		break;
	case 10:/* P1 occupies both bytes */
		ret_val = UnsignedCnvtToDecimal(2,offset);
		break;
	case 51:
	case 113:
	case 114:
	case 115:
	case 116:
	case 118:
	case 123:
	case 124:
		ret_val = 0;
		break;
	case 117:
		ret_val = (int)offset[0];
		break;
	default:
		NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Unknown or unsupported time range indicator detected, continuing");
		return(-1);
	}
	return ((int)(ret_val * c_factor));
}

static int level_comp
#if	NhlNeedProto
(Const void *s1, Const void *s2)
#else
(s1, s2)
void *s1;
void *s2;
#endif
{
	GribRecordInqRecList *s_1 = *(GribRecordInqRecList**)s1;
	GribRecordInqRecList *s_2 = *(GribRecordInqRecList**)s2;

	if((s_1->rec_inq->level0 != -1)&&(s_1->rec_inq->level1 != -1)) {
		if(s_1->rec_inq->level0 == s_2->rec_inq->level0) {
			if(s_1->rec_inq->level1 == s_2->rec_inq->level1) {
				return(s_1->rec_inq->offset - s_2->rec_inq->offset);
			} else {
				return(s_1->rec_inq->level1 - s_2->rec_inq->level1);
			}
		} else {
			return(s_1->rec_inq->level0 - s_2->rec_inq->level0);
		}
	} else {
		if(s_1->rec_inq->level0 == s_2->rec_inq->level0) {
			return(s_1->rec_inq->offset - s_2->rec_inq->offset);
		} else {
			return(s_1->rec_inq->level0 - s_2->rec_inq->level0);
		}
	} 
}
static int date_comp
#if 	NhlNeedProto
(Const void *s1, Const void *s2)
#else
(s1, s2)
void *s1;
void *s2;
#endif
{
	GribRecordInqRecList *s_1 = *(GribRecordInqRecList**)s1;
	GribRecordInqRecList *s_2 = *(GribRecordInqRecList**)s2;
	short result = 0;

	result = s_1->rec_inq->initial_time.year - s_2->rec_inq->initial_time.year;
	if(!result) {
		result = s_1->rec_inq->initial_time.days_from_jan1 - s_2->rec_inq->initial_time.days_from_jan1;
		if(!result) {
			result = s_1->rec_inq->initial_time.minute_of_day - s_2->rec_inq->initial_time.minute_of_day;
			if(!result) {
				result = s_1->rec_inq->time_offset- s_2->rec_inq->time_offset;
			}
		}
		
	} 
	if (! result) {
		return(level_comp(s1,s2));
	}
	return result;
}

static int record_comp
#if 	NhlNeedProto
(Const void *s1, Const void *s2)
#else
(s1, s2)
void *s1;
void *s2;
#endif
{
	GribRecordInqRecList *s_1 = *(GribRecordInqRecList**)s1;
	GribRecordInqRecList *s_2 = *(GribRecordInqRecList**)s2;
	int result = 0;

	if (! s_1->rec_inq->is_ensemble) /* if one is an ensemble they both have to be */
		return date_comp(s1,s2);
	result = s_1->rec_inq->ens.extension_type - s_2->rec_inq->ens.extension_type;
	if (! result) {
		result = s_1->rec_inq->ens.prod_id - s_2->rec_inq->ens.prod_id;
	}
	if (! result) {
		result =  s_1->rec_inq->ens.type - s_2->rec_inq->ens.type;
	}
	if (! result) {
		if (s_1->rec_inq->ens.prob_param && s_2->rec_inq->ens.prob_param) {
			result = s_1->rec_inq->ens.prob_param->num - s_2->rec_inq->ens.prob_param->num;
			if (! result) {
				result =  s_1->rec_inq->ens.prob_type - s_2->rec_inq->ens.prob_type;
			}
			if (! result && (s_1->rec_inq->ens.prob_type == 1 ||  s_1->rec_inq->ens.prob_type == 3)) {
				result =  s_1->rec_inq->ens.lower_prob - s_2->rec_inq->ens.lower_prob;
			}
			if (! result && (s_1->rec_inq->ens.prob_type == 2 ||  s_1->rec_inq->ens.prob_type == 3)) {
				result =  s_1->rec_inq->ens.upper_prob - s_2->rec_inq->ens.upper_prob;
			}
		}
		else {
			result =  s_1->rec_inq->ens.id - s_2->rec_inq->ens.id;
		}
	}
	if (! result) {
		return date_comp(s1,s2);
	}
	return result;
}

static GribParamList *_NewListNode
#if	NhlNeedProto
(GribRecordInqRec *grib_rec)
#else
(grib_rec)
	GribRecordInqRec* grib_rec;
#endif
{
	GribParamList *tmp = NULL;
	GribRecordInqRecList *list = NULL;

	tmp = (GribParamList*)NclMalloc((unsigned)sizeof(GribParamList));
	tmp->next = NULL;
	list = (GribRecordInqRecList*) NclMalloc((unsigned)sizeof(GribRecordInqRecList));
	list->rec_inq = grib_rec;
	list->next = NULL;
	tmp->thelist = list;
	tmp->var_info.var_name_quark = grib_rec->var_name_q;
	if (grib_rec->ptable_rec) {
		tmp->var_info.long_name_q = NrmStringToQuark(grib_rec->ptable_rec->long_name);
		tmp->var_info.units_q = NrmStringToQuark(grib_rec->ptable_rec->units);
	}
	else {
		tmp->var_info.long_name_q = grib_rec->long_name_q;
		tmp->var_info.units_q = grib_rec->units_q;
	}
	tmp->var_info.data_type = GribMapToNcl((void*)&(grib_rec->int_or_float));
	tmp->param_number = grib_rec->param_number;
	tmp->ptable_version = grib_rec->ptable_version;
	tmp->grid_number = grib_rec->grid_number;
	tmp->prob_param = grib_rec->ens.prob_param;
/*
	tmp->grid_number = 255;
*/
	tmp->grid_tbl_index = grib_rec->grid_tbl_index;
	tmp->grid_gds_tbl_index = grib_rec->grid_gds_tbl_index;
	tmp->has_gds= grib_rec->has_gds;
	tmp->gds_type = grib_rec->gds_type;
	tmp->level_indicator = grib_rec->level_indicator;
	tmp->n_entries = 1;
	tmp->minimum_it = grib_rec->initial_time;
	tmp->time_range_indicator = (int)grib_rec->pds[20];
	tmp->time_period = grib_rec->time_period;
	if ((int)grib_rec->pds[17] > 12 && (int)grib_rec->pds[17] != 254) {
		NhlPError(NhlWARNING,NhlEUNKNOWN,
			  "NclGRIB: Unsupported time unit found for parameter (%s), continuing anyways.",
			  NrmQuarkToString(grib_rec->var_name_q));
	}
	tmp->time_unit_indicator = (int)grib_rec->pds[17];
	tmp->variable_time_unit = False;
	
	tmp->probability = NULL;
	tmp->lower_probs = NULL;
	tmp->upper_probs = NULL;
	tmp->levels = NULL;
	tmp->levels0 = NULL;
	tmp->levels1 = NULL;
	tmp->levels_has_two = 0;
	tmp->yymmddhh = NULL;
	tmp->forecast_time = NULL;
	tmp->n_atts = 0;

	/* special processing for DWD */
	if (centers[grib_rec->center_ix].index == 78 &&
	     grib_rec->ptable_version == 205 &&
	     grib_rec->pds[9] == 222 && 
	    (grib_rec->param_number == 3 || grib_rec->param_number == 4)) {
		char buf[256];
		char *desc = NULL;
		char *wave = NULL;
		char *cp;
		int pos;
		char *name = NrmQuarkToString(tmp->var_info.var_name_quark);

		tmp->aux_ids[0] = grib_rec->pds[11];
		tmp->aux_ids[1] = grib_rec->pds[46];
		cp = strchr(name,'_');
		pos = cp - name;
		strcpy(buf,name);
		sprintf(&(buf[pos]),"_K%d_T%d%s",(int)tmp->aux_ids[0],
			(int)tmp->aux_ids[1],&(name[pos]));
		if (grib_rec->param_number == 3) {
			switch ((int)tmp->aux_ids[0]) {
			case 1:
				wave = "Channel 1 (WV 6.4)";
				break;
			case 2:
				wave = "Channel 2 (IR 11.5)";
				break;
			}
		}
		else {
			switch ((int)tmp->aux_ids[0]) {
			case 1:
				wave = "channel 4 (IR 3.9)";
				break;
			case 2:
				wave = "channel 5 (WV 6.2)";
				break;
			case 3:
				wave = "channel 6 (WV 7.3)";
				break;
			case 4:
				wave = "channel 7 (IR 8.7)";
				break;
			case 5:
				wave = "channel 8 (IR 9.7)";
				break;
			case 6:
				wave = "channel 9 (IR 10.8)";
				break;
			case 7:
				wave = "channel 10 (IR 12.1)";
				break;
			case 8:
				wave = "channel 11 (13.4)";
				break;
			}
		}
		tmp->var_info.var_name_quark = NrmStringToQuark(buf);
		tmp->var_info.units_q = NrmStringToQuark("non-dim");
		switch ((int)tmp->aux_ids[1]) {
		case 1:
			desc = "Cloudy brightness temperature";
			break;
		case 2:
			desc = "Clear-sky brightness temperature";
			break;
		case 3:
			desc = "Cloudy radiance";
			break;
		case 4:
			desc = "Clear-sky radiance";
			break;
		}
		sprintf(buf,"%s, %s",desc,wave);
		tmp->var_info.long_name_q = NrmStringToQuark(buf);
	}

	return(tmp);
}

static void _InsertNodeAfter
#if NhlNeedProto
(GribParamList *node, GribParamList *new_node)
#else
(node, new_node)
GribParamList *node; 
GribParamList *new_node;
#endif
{
	GribParamList * tmp;

	tmp = node->next;
	node->next = new_node;
	new_node->next = tmp;
	return;
}

static GribRecordInqRec* _MakeMissingRec
#if NhlNeedProto
(void)
#else
()
#endif
{
	GribRecordInqRec* grib_rec = (GribRecordInqRec*)NclMalloc(sizeof(GribRecordInqRec));

	grib_rec->var_name_q = -1;
	grib_rec->param_number = -1;
	grib_rec->ptable_rec = NULL;
	grib_rec->grid_number = -1;
	grib_rec->grid_tbl_index = -1;
	grib_rec->grid_gds_tbl_index = -1;
	grib_rec->time_offset = -1;
	grib_rec->time_period = -1;
	grib_rec->level0 = -1;
	grib_rec->level1 = -1;
	grib_rec->var_name = NULL;
	grib_rec->long_name_q = -1;
	grib_rec->units_q = -1;
	grib_rec->offset = 0;
	grib_rec->bds_off= 0;
	grib_rec->pds = NULL;
	grib_rec->pds_size = 0;
	grib_rec->gds = NULL;
	grib_rec->bds_size = 0;
	grib_rec->has_gds= 0;
	grib_rec->gds_off= 0;
	grib_rec->gds_size= 0;
	grib_rec->has_bms= 0;
	grib_rec->bms_off= 0;
	grib_rec->bms_size = 0;
	grib_rec->the_dat = NULL;
	grib_rec->interp_method = 0;
	return(grib_rec);
	
}

static void _AddRecordToNode
#if NhlNeedProto
(GribParamList *node, GribRecordInqRec* grib_rec)
#else
(node, grib_rec)
GribParamList *node;
GribRecordInqRec* grib_rec;
#endif
{
	GribRecordInqRecList * grib_rec_list = (GribRecordInqRecList*)NclMalloc((unsigned)sizeof(GribRecordInqRecList));

	

	if((grib_rec->initial_time.year < node->minimum_it.year)

		||((grib_rec->initial_time.year == node->minimum_it.year)	
			&&(grib_rec->initial_time.days_from_jan1 < node->minimum_it.days_from_jan1))

		||((grib_rec->initial_time.year == node->minimum_it.year)
			&&(grib_rec->initial_time.days_from_jan1 == node->minimum_it.days_from_jan1)
			&&(grib_rec->initial_time.minute_of_day < node->minimum_it.minute_of_day))) {

		node->minimum_it = grib_rec->initial_time;

	}
	if(node->time_unit_indicator != (int)grib_rec->pds[17]) {
		_SetCommonTimeUnit(node,grib_rec);
/*
		NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Time unit indicator varies for parameter (%s), continuing anyways.",NrmQuarkToString(grib_rec->var_name_q));
*/
	}
	grib_rec_list->rec_inq = grib_rec;
	grib_rec_list->next = node->thelist;
	node->thelist = grib_rec_list;
	node->n_entries++;
}

static int _IsDef
#if NhlNeedProto
(GribFileRecord *therec, int ptable_version, int param_num)
#else
(therec, param_num)
GribFileRecord *therec;
int param_num;
#endif
{
	GribParamList *step;

	if(therec != NULL) {
	 	step = therec->var_list;
		while(step != NULL) {
			if(step->ptable_version == ptable_version &&
			   step->param_number == param_num) 
				return(1);
			step = step->next;
		
		}
	}
	return(0);
}

static int GridCompare
#if NhlNeedProto
(GribParamList *step, GribRecordInqRec *grib_rec)
#else
(step, grib_rec)
GribbParamList *step;
GribRecordInqRec *grib_rec;
#endif
{
	GribRecordInqRec *compare_rec = step->thelist->rec_inq;
	int r1;

	if (step->grid_number != grib_rec->grid_number)
		return step->grid_number - grib_rec->grid_number;
	if (grib_rec->grid_number < 255)
		return 0;
	if (compare_rec->has_gds != grib_rec->has_gds) 
		return compare_rec->has_gds - grib_rec->has_gds;
	if (! grib_rec->has_gds)
		return 0;

	if (compare_rec->gds_type != grib_rec->gds_type)
		return compare_rec->gds_type - grib_rec->gds_type;
	if (compare_rec->gds_size != grib_rec->gds_size)
		return compare_rec->gds_size - grib_rec->gds_size;
	if (grib_rec->gds_type >= 50 && grib_rec->gds_type < 90)
		return 0;
	/* 
	 * Compare La1 Lo1 La2 Lo2 - hopefully this will give us a definitive answer
	 * Note that since Grib is always big endian a simple memcmp should sort by La1, Lo1, La2, Lo2 
	 */
	r1 = memcmp(&(compare_rec->gds[10]),&(grib_rec->gds[10]),6);
	if (r1 != 0)
		return r1;
	return memcmp(&(compare_rec->gds[17]),&(grib_rec->gds[17]),6);
}


static int _CompareTimePeriod
#if NhlNeedProto
(GribParamList *node, GribRecordInqRec *grib_rec)
#else
(node, grib_rec)
GribParamList *node;
GribRecordInqRec *grib_rec;
#endif
{
	int cix,nix;
	int common_time_unit = 1;
	int time_unit = 1;
	double c_factor = 1.0;

	if (!(node->time_range_indicator < 2 && (int)grib_rec->pds[20] < 2)) {
		if (node->time_range_indicator != (int)grib_rec->pds[20]) {
			return node->time_range_indicator - (int)grib_rec->pds[20];
		}
	}

	if (node->time_unit_indicator == (int)grib_rec->pds[17]) {
		return node->time_period - (int)grib_rec->time_period;
	}

	for (cix = 0; cix < NhlNumber(Unit_Code_Order); cix++) {
		if (node->time_unit_indicator == Unit_Code_Order[cix])
			break;
	}
	for (nix = 0; nix < NhlNumber(Unit_Code_Order); nix++) {
		if ((int)grib_rec->pds[17] == Unit_Code_Order[nix])
			break;
	}
	if (nix >= NhlNumber(Unit_Code_Order)) {
		NhlPError(NhlWARNING,NhlEUNKNOWN,
			  "NclGRIB: Unsupported time unit found for parameter (%s), continuing anyways.",
			  NrmQuarkToString(grib_rec->var_name_q));
	}
	else if (cix >= NhlNumber(Unit_Code_Order)) { 
		/* current time units are unsupported so use the new unit */
		common_time_unit = (int)grib_rec->pds[17];
		time_unit = node->time_unit_indicator;
	}
	else if (Unit_Code_Order[nix] < Unit_Code_Order[cix]) { 
		/* choose the shortest duration as the common unit */
		common_time_unit = (int)grib_rec->pds[17];
		time_unit = node->time_unit_indicator;
	}
	else {
		common_time_unit = node->time_unit_indicator;
		time_unit = (int)grib_rec->pds[17];
	}
		
	if (common_time_unit != time_unit) {
		for (cix = 0; cix < NhlNumber(Unit_Code_Order); cix++) {
			if (common_time_unit == Unit_Code_Order[cix])
				break;
		}
		for (nix = 0; nix < NhlNumber(Unit_Code_Order); nix++) {
			if (time_unit == Unit_Code_Order[nix])
				break;
		}
		/* this condition must be met in order to do a valid conversion */
		if (cix < NhlNumber(Unit_Code_Order) && nix < NhlNumber(Unit_Code_Order)) { 
			c_factor = Unit_Convert[nix] / Unit_Convert[cix];
		}
	}
	if (common_time_unit == node->time_unit_indicator) {
		return node->time_period - (int)grib_rec->time_period * c_factor;
	}
	else {
		return node->time_period * c_factor - (int)grib_rec->time_period;
	}
}
	
static int ParamCompare
#if NhlNeedProto
(GribParamList *node, GribRecordInqRec *grib_rec)
#else
(node, grib_rec)
GribParamList *node;
GribRecordInqRec *grib_rec;
#endif
{
	int comp;

	comp = node->ptable_version - grib_rec->ptable_version;

	if (comp)
		return comp;

	comp = node->param_number - grib_rec->param_number;

	if (comp)
		return comp;

	comp = node->prob_param -  grib_rec->ens.prob_param; 

	if (comp)
		return comp;

	/* special processing for DWD */
	if (!(centers[grib_rec->center_ix].index == 78 &&
	      grib_rec->ptable_version == 205 &&
	      grib_rec->pds[9] == 222 && 
	      (grib_rec->param_number == 3 || grib_rec->param_number == 4)))
		return 0;
	       
	comp = (int) node->aux_ids[0] - (int) grib_rec->pds[11];
	if (comp)
		return comp;
	comp = (int) node->aux_ids[1] - (int) grib_rec->pds[46];

	return comp;
}

static int _FirstCheck
#if NhlNeedProto
(GribFileRecord *therec,GribParamList *step, GribRecordInqRec *grib_rec)
#else
(therec, step, grib_rec)
GribFileRecord *therec;
GribParamList *step;
GribRecordInqRec *grib_rec;
#endif
{
	int comp;

	comp = ParamCompare(step,grib_rec);
	if (comp < 0)
		return 0;
	if(comp > 0) {
		therec->var_list = _NewListNode(grib_rec);
		therec->var_list->next = step;
		therec->n_vars++;
		return(1);
	}

	comp = GridCompare(step,grib_rec);
	if (comp < 0)
		return 0;
	if (comp > 0) {
		therec->var_list = _NewListNode(grib_rec);
		therec->var_list->next = step;
		therec->n_vars++;
		return(1);
	}
#if 0
	if (step->time_range_indicator < (int)grib_rec->pds[20])
		return 0;
	if (step->time_range_indicator > (int)grib_rec->pds[20]) {
		therec->var_list = _NewListNode(grib_rec);
		therec->var_list->next = step;
		therec->n_vars++;
		return(1);
	}
#endif
	comp = _CompareTimePeriod(step,grib_rec);
	if (comp < 0)
		return 0;
	if (comp > 0) {
		therec->var_list = _NewListNode(grib_rec);
		therec->var_list->next = step;
		therec->n_vars++;
		return(1);
	}
	if (step->level_indicator < grib_rec->level_indicator)
		return 0;
	if (step->level_indicator > grib_rec->level_indicator) {
		therec->var_list = _NewListNode(grib_rec);
		therec->var_list->next = step;
		therec->n_vars++;
		return(1);
	}
	_AddRecordToNode(step,grib_rec);
	return(1);
}


static int AdjustedTimePeriod
#if	NhlNeedProto
(GribRecordInqRec *grec, int time_period, int unit_code,char *buf)
#else
(grec,time_period,unit_code,buf)
GribRecordInqRec *grec;
int time_period;
int unit_code;
char * buf;
#endif
{
	int days_per_month[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
	int month, month_days;
	int is_leap = 0;
	int ix;
	/*
	 * Negative time periods are considered to be modular values, and converted to
	 * positive values depending on the units. This is difficult for days, as well
	 * as for any units involving years, where there is no obvious modular value.
         */
	switch (unit_code) {
	case 0: /*Minute*/
		while (time_period < 0)
			time_period = 60 + time_period;
		sprintf(buf,"%dmin",time_period);
		break;
        case 1: /*Hour*/
		while (time_period < 0)
			time_period = 24 + time_period;
		sprintf(buf,"%dh",time_period);
		break;
	case 2: /*Day*/
		/* this oversimplifies and may need attention when there are users of such time periods */
		/* if time period in days == a month, then switch to months */
		month = (int) grec->pds[13];
		month_days = days_per_month[month-1];
		if (month == 2 && HeisLeapYear(grec->initial_time.year)) {
			month_days++;
			is_leap = 1;
		}
		if (grec->pds[20] == 7 && grec->pds[14] - grec->pds[18] == 1 && grec->pds[14] + grec->pds[19] == month_days) {
			/* special processing for GODAS -- although maybe it should be universal --
			   see if we're really talking about a monthly average */
			sprintf(buf,"1m");
			time_period = 1;
		}
		else {
			ix = month - 1;
			while (time_period < 0) {
				if (ix == 1 && is_leap) {
					time_period = days_per_month[ix] + 1 + time_period;
				}
				else {
					time_period = days_per_month[ix] + time_period;
				}
				ix = (ix + 1) % 12;
				if (ix == 0) {
					is_leap =  HeisLeapYear(grec->initial_time.year + 1);
				}
			}
			sprintf(buf,"%dd",time_period);
		}
		break;
	case 3: /*Month*/
		while (time_period < 0)
			time_period = 12 + time_period;
		sprintf(buf,"%dm",time_period);
		break;
	case 4: /*Year*/
		time_period = abs(time_period);
		sprintf(buf,"%dy",time_period);
		break;
	case 5: /*Decade (10 years)*/
		time_period = abs(time_period);
		sprintf(buf,"%dy",time_period * 10);
		break;
	case 6: /*Normal (30 years)*/
		time_period = abs(time_period);
		sprintf(buf,"%dy",time_period * 30);
		break;
	case 7: /*Century*/
		time_period = abs(time_period);
		sprintf(buf,"%dy",time_period * 100);
		break;
	case 10: /*3 hours*/
		time_period *= 3;
		while (time_period < 0)
			time_period = 24 + time_period;
		sprintf(buf,"%dh",time_period);
		time_period /= 3;
		break;
	case 11: /*6 hours*/
		time_period *= 6;
		while (time_period < 0)
			time_period = 24 + time_period;
		sprintf(buf,"%dh",time_period);
		time_period /= 6;
		break;
	case 12: /*12 hours*/
		time_period *= 12;
		while (time_period < 0)
			time_period = 24 + time_period;
		sprintf(buf,"%dh",time_period);
		time_period /= 12;
		break;
	case 13: /*15 minutes*/
		time_period *= 15;
		while (time_period < 0)
			time_period = 60 + time_period;
		sprintf(buf,"%dmin",time_period);
		time_period /= 15;
		break;
	case 14: /*30 minutes*/
		time_period *= 30;
		while (time_period < 0)
			time_period = 60 + time_period;
		sprintf(buf,"%dmin",time_period);
		time_period /= 30;
		break;
	case 254: /*Second*/
		while (time_period < 0)
			time_period = 60 + time_period;
		sprintf(buf,"%dsec",time_period);
		break;
	default: /*unknown*/
		time_period = abs(time_period);
		sprintf(buf,"%d",time_period);
		break;
	}
	return time_period;
}



static PtableInfo *InitParamTableInfo
#if	NhlNeedProto
(
int center,
int subcenter,
int version,
char *tablename
)
#else
(center,subcenter,version,name)
int center;
int subcenter;
int version;
char *tablename;
#endif
{
	PtableInfo *ptable = NhlMalloc(sizeof(PtableInfo));

	if (ptable) {
		ptable->table = NclMalloc(256 * sizeof(TBLE2));
	}
	if (! ptable || ! ptable->table) {
		NHLPERROR((NhlFATAL,ENOMEM,NULL));
		return NULL;
	}
	ptable->pcount = 0;
	ptable->center = center;
	ptable->subcenter = subcenter;
	ptable->version = version;
	ptable->name = tablename;

	return ptable;
}

#define EATSPACE(cp) if (cp) {while (isspace(*(cp))) {(cp)++;}}
#define EATSPACE_REV(cp) if (cp) {while (isspace(*(cp))) {(cp)--;}}
#define TOKENSTART(cp) if ((cp) && *(cp)) {(cp) = strstr((cp),sepstr); if (cp) (cp) += seplen; EATSPACE(cp)}
#define TOKENEND(cp) if (*(cp)) {char *endcp = strstr((cp),sepstr); if (endcp) (cp) = endcp-1; \
				else cp = (cp)+strlen(cp)-1; EATSPACE_REV(cp)}

		
static PtableInfo *AddPtable
#if	NhlNeedProto
(
PtableInfo *ptables,
FILE *fp,
char *name
)
#else
(ptables,fp,name)
PtableInfo *ptables;
FILE *fp;
char *name;
#endif
{
	char buf[512];
	int center = -1 ,subcenter = -1 ,version = -1;
	int index;
	char *cp,*lcp;
	char sepstr[3] = "";
	int seplen = 1;
	int len;
	char *tablename = NULL;
	TBLE2 *param;
	char *abrev, *units, *long_name;
	PtableInfo *ptable = NULL;
	int table_count = 0;
	
	while (fgets(buf,510,fp)) {
		cp = buf;
		EATSPACE(cp);
		if (*cp == '!')
			continue;
		index = strtol(cp,&lcp,10);
		if (lcp == cp)
			continue;
		cp = lcp;

		/*
		 * Look for a separator string: it's either a single non-space, non-alphanumeric character; or
		 * it's at least two space characters in a row.
		 */
		  
		if (! sepstr[0]) { 
			EATSPACE(cp);
			if (! isalnum(*cp)) {
				sepstr[0] = *cp;
				sepstr[1] = '\0';
				seplen = 1;
			}
			else if (cp - lcp > 1) {
				sepstr[0] = ' ';
				sepstr[1] = ' ';
				sepstr[2] = '\0';
				seplen = 2;
			}
			else { /* no sepstr : this is not a valid parameter table */
				return ptables;
			}
			cp = lcp;
		}
		if (index == -1) {
			TOKENSTART(cp);
			if (cp)
				center =  strtol(cp,&cp,10);
			TOKENSTART(cp);
			if (cp)
				subcenter =  strtol(cp,&cp,10);
			TOKENSTART(cp);
			if (cp)
				version =  strtol(cp,&cp,10);
			TOKENSTART(cp);
			if (cp) {
				lcp = cp;
				TOKENEND(lcp);
				len = lcp - cp + 1;
				if (len > 0) {
					tablename = NclMalloc(len + 1);
					strncpy(tablename,cp,len);
					tablename[len] = '\0';
				}
				cp = lcp + 1;
			}
			else {
				/* use the base filename for the tablename */
				if (table_count) {
					char buf[10];
					sprintf(buf,"_%d",table_count);
					tablename = NclMalloc(strlen(name)+strlen(buf)+1);
					sprintf(tablename,"%s%s",name,buf);
				}
				else {
					tablename = NclMalloc(strlen(name)+1);
					strcpy(tablename,name);
				}				
			}
			table_count++;
			/*
			 * we have a new table: if this file contains several,
			 * wrap up the previous one and initalize the new one
			 */
			if (ptable) {
				ptable->next = ptables;
				ptables = ptable;
				ptable->table = NclRealloc(ptable->table, ptable->pcount * sizeof(TBLE2));
			}
			ptable = InitParamTableInfo(center,subcenter,version,tablename);
			if (! ptable)
				return (ptables);
			continue;
		}
		else if (index < -1 || index > 255) {
			/* ignore */
			continue;
		}
		param = &(ptable->table[ptable->pcount++]);
		param->num = index;
		TOKENSTART(cp);
		if (cp) {
			lcp = cp;
			TOKENEND(lcp);
			len = MAX(0,lcp - cp + 1);
			abrev = NclMalloc(len + 1);
			if (len > 0)
				strncpy(abrev,cp,len);
			abrev[len] = '\0';
			cp = lcp + 1;
			param->abrev = abrev;
		}
		TOKENSTART(cp);
		if (cp) {
			lcp = cp;
			TOKENEND(lcp);
			len = MAX(0,lcp - cp + 1);
			units = NclMalloc(len + 1);
			if (len > 0)
				strncpy(units,cp,len);
			units[len] = '\0';
			cp = lcp + 1;
			param->units = units;
		}
		TOKENSTART(cp);
		if (cp) {
			lcp = cp;
			TOKENEND(lcp);
			len = MAX(0,lcp - cp + 1);
			long_name = NclMalloc(len + 1);
			if (len > 0)
				strncpy(long_name,cp,len);
			long_name[len] = '\0';
			cp = lcp + 1;
			param->long_name = long_name;
		}
	}
	if (ptable) {
		ptable->next = ptables;
		ptables = ptable;
		ptable->table = NclRealloc(ptable->table, ptable->pcount * sizeof(TBLE2));
	}
	return ptables;
	
}
		

static void InitPtables
#if	NhlNeedProto
(void)
#else
()
#endif
{
	char *path;
	DIR *d;
	FILE *fp = NULL;
	PtableInfo *ptables = NULL;

	struct dirent *ent;
	char buffer[4*NCL_MAX_STRING];
 
	path = getenv("NCL_GRIB_PTABLE_PATH");

	if (! path) {
		path = getenv("NIO_GRIB_PTABLE_PATH");
                if (! path)
			return;
	}
		
	d = opendir(_NGResolvePath(path));
	if (! d && errno == ENOTDIR) {
		fp = fopen(_NGResolvePath(path),"r");
		if (!fp) 
			return;
	}
	else if (! d) {
		return;
	}

	if (fp) { /* getenv returned a filepath */
		char *s = strrchr(path,'/');
		char *e = strstr(path,".gtb");
		if (! s)
			s = path;
		else
			s++;
		if (! e)
			e = &(path[strlen(path)]);
		strncpy(buffer,s,e-s);
		buffer[e-s] = '\0';
		ptables = AddPtable(ptables,fp,buffer);
	}
	else {
		while((ent = readdir(d)) != NULL) {
			char *e;
			if ((&(ent->d_name[strlen(ent->d_name)]) - strstr(ent->d_name,".gtb")  != 4))
				continue;
			sprintf(buffer,"%s/%s",_NGResolvePath(path),ent->d_name);
			fp = fopen(buffer,"r");
			if (! fp)
				continue;
			e = strstr(ent->d_name,".gtb");
			strncpy(buffer,ent->d_name,e-ent->d_name);
			buffer[e-ent->d_name] = '\0';
			ptables = AddPtable(ptables,fp,buffer);
		}
	}

	Ptables = ptables;

	if (0) {
		/* debugging */
		PtableInfo *ptable;
		int i;
		TBLE2 *param;

		for (ptable = Ptables; ptable; ptable = ptable->next) {
			printf("Table: %s, center: %d, subcenter %d, Ptable version: %d\n",
			       ptable->name,ptable->center,ptable->subcenter,ptable->version);
			for (i = 0; i < ptable->pcount; i++) {
				param = &(ptable->table[i]);
				printf("\t%d %s %s %s\n",param->num,param->abrev,param->units,param->long_name);
			}
		}
	}

	return;
}


static int InitializeOptions 
#if	NhlNeedProto
(GribFileRecord *tmp)
#else
(tmp)
GribFileRecord *tmp;
#endif
{
	GribOptions *options;

	tmp->n_options = GRIB_NUM_OPTIONS;
	
	options = NclMalloc(tmp->n_options * sizeof(GribOptions));
	if (! options) {
		NhlPError(NhlFATAL,ENOMEM,NULL);
		return 0;
	}
	options[GRIB_THINNED_GRID_INTERPOLATION_OPT].data_type = NCL_string;
	options[GRIB_THINNED_GRID_INTERPOLATION_OPT].n_values = 1;
	options[GRIB_THINNED_GRID_INTERPOLATION_OPT].values = (void *) NrmStringToQuark("cubic");

	options[GRIB_INITIAL_TIME_COORDINATE_TYPE_OPT].data_type = NCL_string;
	options[GRIB_INITIAL_TIME_COORDINATE_TYPE_OPT].n_values = 1;
	options[GRIB_INITIAL_TIME_COORDINATE_TYPE_OPT].values = (void *) NrmStringToQuark("numeric");

	options[GRIB_DEFAULT_NCEP_PTABLE_OPT].data_type = NCL_string;
	options[GRIB_DEFAULT_NCEP_PTABLE_OPT].n_values = 1;
	options[GRIB_DEFAULT_NCEP_PTABLE_OPT].values = (void *) NrmStringToQuark("operational");

	options[GRIB_PRINT_RECORD_INFO_OPT].data_type = NCL_logical;
	options[GRIB_PRINT_RECORD_INFO_OPT].n_values = 1;
	options[GRIB_PRINT_RECORD_INFO_OPT].values = (void *) 0;

	options[GRIB_SINGLE_ELEMENT_DIMENSIONS_OPT].data_type = NCL_string;
	options[GRIB_SINGLE_ELEMENT_DIMENSIONS_OPT].n_values = 1;
	options[GRIB_SINGLE_ELEMENT_DIMENSIONS_OPT].values = (void *) NrmStringToQuark("none");

	options[GRIB_TIME_PERIOD_SUFFIX_OPT].data_type = NCL_logical;
	options[GRIB_TIME_PERIOD_SUFFIX_OPT].n_values = 1;
	options[GRIB_TIME_PERIOD_SUFFIX_OPT].values = (void *) 1;

	tmp->options = options;
	return 1;
}

static void *GribInitializeFileRec
#if	NhlNeedProto
(NclFileFormat *format)
#else
(format)
NclFileFormatType *format;
#endif
{
	GribFileRecord *therec = NULL;

	therec = (GribFileRecord*)NclCalloc(1, sizeof(GribFileRecord));
	if (! therec) {
		NhlPError(NhlFATAL,ENOMEM,NULL);
		return NULL;
	}
	InitializeOptions(therec);
	*format = _NclGRIB;
	return (void *) therec;
}

static void *GribOpenFile
#if	NhlNeedProto
(void *rec,NclQuark path,int wr_status)
#else
(rec,path,wr_status)
void *rec;
NclQuark path;
int wr_status;
#endif
{
	int fd;
	int done = 0;
	off_t offset = 0;
	off_t nextoff = 0;
	unsigned int size = 0;
	GribFileRecord *therec = (GribFileRecord *)rec;
	GribRecordInqRec *grib_rec = NULL;
	GribRecordInqRecList *grib_rec_list = NULL;
	GribParamList *step = NULL,*step2 = NULL, *tmpstep = NULL ;
	int i,k;
	int ret;
	unsigned char tmpc[4];
	unsigned char buffer[80];
	GribRecordInqRecList **sortar;
	TBLE2 *name_rec = NULL;
	TBLE2 *tmp_name_rec = NULL;
	int version;
	int error_count = 0;
	int rec_num = 0;
	int subcenter, center, process,ptable_version;
	NhlErrorTypes retvalue;
	struct stat statbuf;
	int suffix;
	int table_warning = 0;

	if (! Ptables) {
		InitPtables();
		_DateInit();
	}

	therec->n_vars = 0;
	therec->var_list = NULL;
	therec->wr_status = wr_status;	
	therec->file_path_q = path;
	therec->internal_var_list = NULL;
	therec->n_internal_vars = 0;

	if(wr_status <= 0) {
		NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Grib files are read only continuing but opening file as read only");
	}
	if(stat(NrmQuarkToString(path),&statbuf) == -1) {
		NhlPError(NhlFATAL, NhlEUNKNOWN,"NclGRIB: Unable to open input file (%s)",NrmQuarkToString(path));
		return NULL;
	}
	fd = open(NrmQuarkToString(path),O_RDONLY);
	vbuflen = statbuf.st_blksize;
	vbuf = (void*)NclMalloc(vbuflen);

	/*
	setvbuf(fd,vbuf,_IOFBF,4*getpagesize());
	*/

	
	if (fd > 0) {
		while(!done) {
			ret = GetNextGribOffset(fd,&offset,&size,offset,&nextoff,&version);
			if(ret == GRIBEOF) {
				done = 1;
			} 
			if((ret != GRIBERROR)&&(size != 0)) {
				rec_num++;
				grib_rec = NclMalloc((unsigned)sizeof(GribRecordInqRec));
				grib_rec->rec_num = rec_num;
				grib_rec->gds = NULL;
				grib_rec->the_dat = NULL;
				grib_rec->version = version;
				grib_rec->var_name = NULL;
				lseek(fd,offset+(version?8:4),SEEK_SET);
				read(fd,(void*)vbuf,vbuflen);
				vbufpos = 0;
				grib_rec->pds_size = CnvtToDecimal(3,&(vbuf[0]));
				if (grib_rec->pds_size <= 0 || grib_rec->pds_size > size 
					|| grib_rec->pds_size > vbuflen) {
					NhlPError(NhlWARNING, NhlEUNKNOWN, 
						  "NclGRIB: Detected invalid record, skipping record");
					NclFree(grib_rec);
					offset = nextoff;
					continue;
				}
					
				grib_rec->pds =  NclMalloc((unsigned)grib_rec->pds_size);
				memcpy(grib_rec->pds,vbuf,grib_rec->pds_size);
				vbufpos += grib_rec->pds_size;
/*
				fprintf(stdout,"Found: %d\n",(int)(int)grib_rec->pds[8]);
*/
				grib_rec->has_gds = (grib_rec->pds[7] & (char)0200) ? 1 : 0;
				grib_rec->has_bms = (grib_rec->pds[7] & (char)0100) ? 1 : 0;
				grib_rec->param_number = (int)grib_rec->pds[8];
				grib_rec->grid_number = (int)grib_rec->pds[6];
				center = (int)grib_rec->pds[4];
				subcenter = (int)grib_rec->pds[25];
				process = (int)grib_rec->pds[5];
				ptable_version = (int)grib_rec->pds[3];
				grib_rec->ptable_version = ptable_version;
				grib_rec->eff_center = -1; 
				grib_rec->center_ix = -1;
				for (i = 0; i < sizeof(centers)/sizeof(GribTable); i++) {
				  if (centers[i].index == (int) grib_rec->pds[4]) {
				    grib_rec->center_ix = i;
				    break;
				  }
				}
				if (subcenter == 98) {
					for (i = 0; i < sizeof(ecmwf_members)/ sizeof(int); i++) {
						if (center != ecmwf_members[i])
							continue;
						grib_rec->eff_center = 98;
						break;
					}
				}
/*
				if((grib_rec->has_gds) && (grib_rec->grid_number != 255)) {
					fprintf(stdout,"Found one: %d\n",grib_rec->grid_number);
				} 
				if(grib_rec->has_bms) {
					fprintf(stdout,"Found one with bms (%d,%d)\n",grib_rec->param_number,grib_rec->grid_number);
				}
*/
				grib_rec->offset = offset;

				if(version) {
					grib_rec->initial_time.year = (short)(((short)grib_rec->pds[24] - 1 )*100 + (short)(int)grib_rec->pds[12]);
					grib_rec->initial_time.days_from_jan1 = HeisDayDiff(1,1,grib_rec->initial_time.year,(int)grib_rec->pds[14],(int)grib_rec->pds[13],grib_rec->initial_time.year);
					grib_rec->initial_time.minute_of_day = (short)grib_rec->pds[15] * 60 + (short)grib_rec->pds[16];
				} else {
					grib_rec->initial_time.year = (short)(1900 + (short)(int)grib_rec->pds[12]);
					grib_rec->initial_time.days_from_jan1 = HeisDayDiff(1,1,grib_rec->initial_time.year,(int)grib_rec->pds[14],(int)grib_rec->pds[13],grib_rec->initial_time.year);
					grib_rec->initial_time.minute_of_day = (short)grib_rec->pds[15] * 60 + (short)grib_rec->pds[16];
				}
				if(grib_rec->version != 0) {
					if(((int)grib_rec->pds[24] < 1)|| ((int)grib_rec->pds[15] > 24)||((int)grib_rec->pds[16] > 60)||((int)grib_rec->pds[13] > 12)||((int)grib_rec->pds[12] > 100)){
						/*
						NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Corrupted record found. Time values out of appropriate ranges, skipping record");
						NhlFree(grib_rec);
						grib_rec = NULL;
						*/

					}
				} else {
					if(((int)grib_rec->pds[15] > 24)||((int)grib_rec->pds[16] > 60)||((int)grib_rec->pds[13] > 12)||((int)grib_rec->pds[12] > 100)){
						NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Corrupted record found. Time values out of appropriate ranges, skipping record");
						NhlFree(grib_rec);
						grib_rec = NULL;

					}
				}

				if(grib_rec != NULL) {
					grib_rec->time_offset = 0;
					if ((NrmQuark)(therec->options[GRIB_THINNED_GRID_INTERPOLATION_OPT].values) == 
					    NrmStringToQuark("cubic")) {
						grib_rec->interp_method = 1;
					}
					else {
						grib_rec->interp_method = 0;
					}
					if(grib_rec->has_gds) {
						grib_rec->gds_off = (version?8:4) + grib_rec->pds_size;
						if (vbuflen - vbufpos > 6) {
							grib_rec->gds_size = CnvtToDecimal(3,&(vbuf[vbufpos]));
							grib_rec->gds_type = (int)vbuf[vbufpos + 5];
						}
						else {
							lseek(fd,(unsigned)(grib_rec->offset + (version?8:4) + grib_rec->pds_size),SEEK_SET);
							read(fd,(void*)vbuf,6);
							grib_rec->gds_size = CnvtToDecimal(3,vbuf);
							grib_rec->gds_type = (int)vbuf[5];
						}
						/*
						  fprintf(stdout,"%d\n",grib_rec->gds_type);
						*/
						grib_rec->gds = (unsigned char*)NclMalloc((unsigned)sizeof(char)*grib_rec->gds_size);
						if (vbuflen - vbufpos > grib_rec->gds_size) {
							memcpy(grib_rec->gds,&(vbuf[vbufpos]),grib_rec->gds_size);
						}
						else {
							lseek(fd,(unsigned)(grib_rec->offset + (version?8:4) + grib_rec->pds_size),SEEK_SET);
							read(fd,(void*)grib_rec->gds,grib_rec->gds_size);
						}
						vbufpos += grib_rec->gds_size;
					} else {
						grib_rec->gds_off = 0;	
						grib_rec->gds_size = 0;
						grib_rec->gds_type = -1;
						grib_rec->gds = NULL;
					}
					if((grib_rec->has_gds) && (grib_rec->grid_number == 255 || grib_rec->grid_number == 0) ) { 
						for(i = 0; i < grid_gds_tbl_len ; i++) {
							if(grib_rec->gds_type == grid_gds_index[i]) { 
								grib_rec->grid_gds_tbl_index = i;
								break;
							}
						}
						if(i == grid_gds_tbl_len) {
							grib_rec->grid_gds_tbl_index = 0;
						}
						grib_rec->grid_tbl_index = -1;
					} else {
						grib_rec->grid_tbl_index = -1;
						for(i = 0; i < grid_tbl_len ; i++) {
							if(grib_rec->grid_number == grid_index[i]) { 
								grib_rec->grid_tbl_index = i;
								break;
							}
						}
						if(grib_rec->has_gds) {
							/*
							if((i == grid_tbl_len) || (grid[grib_rec->grid_tbl_index].get_grid == NULL)){
								grib_rec->grid_tbl_index = -1;
							}
							*/
							for(i = 0; i < grid_gds_tbl_len ; i++) {
								if(grib_rec->gds_type == grid_gds_index[i]) { 
									grib_rec->grid_gds_tbl_index = i;
									break;
								}
							}
							if(i == grid_gds_tbl_len) {
								grib_rec->grid_gds_tbl_index = 0;
							}
						}
						else {
							grib_rec->grid_gds_tbl_index = 0;
						}
					}

					if(grib_rec->has_bms) {
						if (vbuflen - vbufpos > 3) {
							grib_rec->bms_size = CnvtToDecimal(3,&(vbuf[vbufpos]));
						}
						else {
							lseek(fd,(unsigned)(grib_rec->offset + (version?8:4) + grib_rec->pds_size + grib_rec->gds_size),SEEK_SET);
							read(fd,(void*)tmpc,3);
							grib_rec->bms_size = CnvtToDecimal(3,tmpc);
						}
						grib_rec->bms_off = (version?8:4) + grib_rec->pds_size + grib_rec->gds_size;
					} else {
						grib_rec->bms_off = 0;
						grib_rec->bms_size = 0;
					}
					grib_rec->bds_off = (version ? 8:4) + grib_rec->pds_size + grib_rec->bms_size + grib_rec->gds_size;
					vbufpos += grib_rec->bms_size;
					if (vbuflen - vbufpos > 3) {
						grib_rec->bds_size = CnvtToDecimal(3,&(vbuf[vbufpos]));
						grib_rec->bds_flags = vbuf[vbufpos+3];
						grib_rec->int_or_float = (int)(grib_rec->bds_flags & (char)0040) ? 1 : 0;
					}
					else {
						lseek(fd,(unsigned)(grib_rec->offset + grib_rec->bds_off),SEEK_SET);
						read(fd,(void*)tmpc,4);
						grib_rec->bds_flags = tmpc[3];
						grib_rec->bds_size = CnvtToDecimal(3,tmpc);
						grib_rec->int_or_float = (int)(tmpc[3]  & (char)0040) ? 1 : 0;
					}
				}

	
				name_rec = NULL;	
				if (grib_rec != NULL) {
					TBLE2 *ptable = NULL;
					int ptable_count = 0;
					int lcenter = center;
					if (grib_rec->eff_center > 0)
						lcenter = grib_rec->eff_center;

					switch(lcenter) {
					case 98:           /* ECMWF */
						switch (ptable_version) {
						case 0:
						case 128:
							ptable = &ecmwf_128_params[0];
							ptable_count = sizeof(ecmwf_128_params)/sizeof(TBLE2);
							break;
						case 129:
							ptable = &ecmwf_129_params[0];
							ptable_count = sizeof(ecmwf_129_params)/sizeof(TBLE2);
							break;
						case 130:
							ptable = &ecmwf_130_params[0];
							ptable_count = sizeof(ecmwf_130_params)/sizeof(TBLE2);
							break;
						case 131:
							ptable = &ecmwf_131_params[0];
							ptable_count = sizeof(ecmwf_131_params)/sizeof(TBLE2);
							break;
						case 132:
							ptable = &ecmwf_132_params[0];
							ptable_count = sizeof(ecmwf_132_params)/sizeof(TBLE2);
							break;
						case 133:
							ptable = &ecmwf_133_params[0];
							ptable_count = sizeof(ecmwf_133_params)/sizeof(TBLE2);
							break;
						case 140:
							ptable = &ecmwf_140_params[0];
							ptable_count = sizeof(ecmwf_140_params)/sizeof(TBLE2);
							break;
						case 150:
							ptable = &ecmwf_150_params[0];
							ptable_count = sizeof(ecmwf_150_params)/sizeof(TBLE2);
							break;
						case 151:
							ptable = &ecmwf_151_params[0];
							ptable_count = sizeof(ecmwf_151_params)/sizeof(TBLE2);
							break;
						case 160:
							ptable = &ecmwf_160_params[0];
							ptable_count = sizeof(ecmwf_160_params)/sizeof(TBLE2);
							break;
						case 162:
							ptable = &ecmwf_162_params[0];
							ptable_count = sizeof(ecmwf_162_params)/sizeof(TBLE2);
							break;
						case 170:
							ptable = &ecmwf_170_params[0];
							ptable_count = sizeof(ecmwf_170_params)/sizeof(TBLE2);
							break;
						case 171:
							ptable = &ecmwf_171_params[0];
							ptable_count = sizeof(ecmwf_171_params)/sizeof(TBLE2);
							break;
						case 172:
							ptable = &ecmwf_172_params[0];
							ptable_count = sizeof(ecmwf_172_params)/sizeof(TBLE2);
							break;
						case 173:
							ptable = &ecmwf_173_params[0];
							ptable_count = sizeof(ecmwf_173_params)/sizeof(TBLE2);
							break;
						case 174:
							ptable = &ecmwf_174_params[0];
							ptable_count = sizeof(ecmwf_174_params)/sizeof(TBLE2);
							break;
						case 175:
							ptable = &ecmwf_175_params[0];
							ptable_count = sizeof(ecmwf_175_params)/sizeof(TBLE2);
							break;

						case 180:
							ptable = &ecmwf_180_params[0];
							ptable_count = sizeof(ecmwf_180_params)/sizeof(TBLE2);
							break;
						case 190:
							ptable = &ecmwf_190_params[0];
							ptable_count = sizeof(ecmwf_190_params)/sizeof(TBLE2);
							break;
						case 200:
							ptable = &ecmwf_200_params[0];
							ptable_count = sizeof(ecmwf_200_params)/sizeof(TBLE2);
							break;
						case 201:
							ptable = &ecmwf_201_params[0];
							ptable_count = sizeof(ecmwf_201_params)/sizeof(TBLE2);
							break;
						case 210:
							ptable = &ecmwf_210_params[0];
							ptable_count = sizeof(ecmwf_210_params)/sizeof(TBLE2);
							break;
						case 211:
							ptable = &ecmwf_211_params[0];
							ptable_count = sizeof(ecmwf_211_params)/sizeof(TBLE2);
							break;
						case 228:
							ptable = &ecmwf_228_params[0];
							ptable_count = sizeof(ecmwf_228_params)/sizeof(TBLE2);
							break;
						case 230:
							ptable = &ecmwf_230_params[0];
							ptable_count = sizeof(ecmwf_230_params)/sizeof(TBLE2);
							break;
						case 234:
							ptable = &ecmwf_234_params[0];
							ptable_count = sizeof(ecmwf_234_params)/sizeof(TBLE2);
							break;

						}
						break;
					case 78: /* DWD */
					case 146: /* Brazilian Navy Hydrographic Center -- uses DWD tables according to wgrib */
						switch (ptable_version) {
						case 2:
							ptable = &dwd_002_params[0];
							ptable_count = sizeof(dwd_002_params)/sizeof(TBLE2);
							break;
						case 201:
							ptable = &dwd_201_params[0];
							ptable_count = sizeof(dwd_201_params)/sizeof(TBLE2);
							break;
						case 202:
							ptable = &dwd_202_params[0];
							ptable_count = sizeof(dwd_202_params)/sizeof(TBLE2);
							break;
						case 203:
							ptable = &dwd_203_params[0];
							ptable_count = sizeof(dwd_203_params)/sizeof(TBLE2);
							break;
						case 204:
							ptable = &dwd_204_params[0];
							ptable_count = sizeof(dwd_204_params)/sizeof(TBLE2);
							break;
						case 205:
							ptable = &dwd_205_params[0];
							ptable_count = sizeof(dwd_205_params)/sizeof(TBLE2);
							break;
						case 206:
							ptable = &dwd_206_params[0];
							ptable_count = sizeof(dwd_206_params)/sizeof(TBLE2);
							break;
						case 207:
							ptable = &dwd_207_params[0];
							ptable_count = sizeof(dwd_207_params)/sizeof(TBLE2);
							break;
						}
						break;
					case 58: /* FNMOC */
						ptable = &fnmoc_params[0];
						ptable_count = sizeof(fnmoc_params)/sizeof(TBLE2);
						break;
					case 46: /* CPTEC */
						if (ptable_version == 254) {
							ptable = &cptec_254_params[0];
							ptable_count = sizeof(cptec_254_params)/sizeof(TBLE2);
						}
						break;
					case 34: /* JMA */
						if (ptable_version == 3) {
							ptable = &jma_3_params[0];
							ptable_count = sizeof(jma_3_params)/sizeof(TBLE2);
						}
						break;
					case 8:
					case 9: /* NCEP reanalysis */
						ptable = &ncep_reanal_params[0];
						ptable_count = sizeof(ncep_reanal_params)/sizeof(TBLE2);
						break;
					case 59: /* FSL */
						if (ptable_version < 128) {
							switch (subcenter) {
							case 0: /* FSL: The NOAA Forecast Systems Laboratory, Boulder, CO, USA */
								ptable = &fsl0_params[0];
								ptable_count = sizeof(fsl0_params)/sizeof(TBLE2);
								break;
							case 1: /* RAPB: FSL/FRD Regional Analysis and Prediction Branch */
								ptable = &fsl1_params[0];
								ptable_count = sizeof(fsl1_params)/sizeof(TBLE2);
								break;
							case 2: /* LAPB: FSL/FRD Local Analysis and Prediction Branch */
								ptable = &fsl2_params[0];
								ptable_count = sizeof(fsl2_params)/sizeof(TBLE2);
								break;
							}
							break;
						}
						/* fall through to NCEP tables for table versions above 127 */
					case 7: /* NCEP */
					case 60: /* NCAR */ 
						switch (ptable_version) {
						case 0:
						case 1:
						case 2:
						case 3:
							if (subcenter == 1) {
								/* reanalysis */
								ptable = &ncep_reanal_params[0];
								ptable_count = sizeof(ncep_reanal_params)/sizeof(TBLE2);
							}
							else if (subcenter != 0 || (process != 80 && process != 180) ||
								 (ptable_version != 1 && ptable_version != 2)) {
								/* operational */
								ptable = &ncep_opn_params[0];
								ptable_count = sizeof(ncep_opn_params)/sizeof(TBLE2);
							}
							else {
								/* not able to tell -- use the default value */
								if ((NrmQuark)(therec->options[GRIB_DEFAULT_NCEP_PTABLE_OPT].values) 
								    == NrmStringToQuark("reanalysis")) {
									ptable = &ncep_reanal_params[0];
									ptable_count = sizeof(ncep_reanal_params)/sizeof(TBLE2);
								}
								else {
									ptable = &ncep_opn_params[0];
									ptable_count = sizeof(ncep_opn_params)/sizeof(TBLE2);
								}
							}
							
							break;
						case 128: /* ocean modeling branch */
							ptable = &ncep_128_params[0];
							ptable_count = sizeof(ncep_128_params)/sizeof(TBLE2);
							break;
						case 129: 
							ptable = &ncep_129_params[0];
							ptable_count = sizeof(ncep_129_params)/sizeof(TBLE2);
							break;
						case 130: 
							ptable = &ncep_130_params[0];
							ptable_count = sizeof(ncep_130_params)/sizeof(TBLE2);
							break;
						case 131: 
							ptable = &ncep_131_params[0];
							ptable_count = sizeof(ncep_131_params)/sizeof(TBLE2);
							break;
					        case 132:
							ptable = &ncep_reanal_params[0];
							ptable_count = sizeof(ncep_reanal_params)/sizeof(TBLE2);
							break;
						case 133: 
							ptable = &ncep_133_params[0];
							ptable_count = sizeof(ncep_133_params)/sizeof(TBLE2);
							break;
						case 140: 
							ptable = &ncep_140_params[0];
							ptable_count = sizeof(ncep_140_params)/sizeof(TBLE2);
							break;
						case 141: 
							ptable = &ncep_141_params[0];
							ptable_count = sizeof(ncep_141_params)/sizeof(TBLE2);
							break;
						}
						break;
					}
					/*
					 * if there are user-provided tables see if any match. (-1 matches anything)
					 *
					 */
					if (Ptables) {
						PtableInfo *pt;
						for (pt = Ptables; pt != NULL; pt = pt->next) {
							if (!((pt->center == -1 || pt->center == center) &&
							      (pt->subcenter == -1 || pt->subcenter == subcenter) &&
							      (pt->version == -1 || pt->version == ptable_version)))
								continue;
							ptable = pt->table;
							ptable_count = pt->pcount;
							break;
						}
					}
					if (ptable == NULL && grib_rec->param_number < 128) {
						if (ptable_version > 3 && table_warning == 0) {
							NhlPError(NhlWARNING,NhlEUNKNOWN,
								  "NclGRIB: Unrecognized parameter table (center %d, subcenter %d, table %d), defaulting to NCEP operational table for standard parameters (1-127): variable names and units may be incorrect",
								  center, subcenter, ptable_version);
							table_warning = 1;
						}
						/* 
						 * if the ptable_version <= 3 and the parameter # is less than 128 then 
						 * the NCEP operational table is the legitimate default; 
						 */
						ptable = &ncep_opn_params[0];
						ptable_count = sizeof(ncep_opn_params)/sizeof(TBLE2);
					}
					for (i = 0; i < ptable_count; i++) {
						if (ptable[i].num == grib_rec->param_number) {
							name_rec = ptable + i;
							break;
						}
					}
					if (i == ptable_count) {
						if(!_IsDef(therec,grib_rec->ptable_version,grib_rec->param_number)) {
							NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Unknown grib parameter number detected (%d, center %d, table version %d grib record %d), using default variable name (VAR_%d)",
								  grib_rec->param_number,
								  center,
								  ptable_version,
								  rec_num,
								  grib_rec->param_number);
						}
						i = -1;
					}
				}
					
				if((name_rec == NULL)&&(grib_rec!=NULL)) {
					tmp_name_rec = NclMalloc(sizeof(TBLE2));
					tmp_name_rec->abrev = NclMalloc(strlen("VAR_") + 4);
					sprintf(tmp_name_rec->abrev,"VAR_%d",grib_rec->param_number);
					tmp_name_rec->long_name = NclMalloc(strlen("Unknown Variable Name") + 1);
					sprintf(tmp_name_rec->long_name,"Unknown Variable Name");
					tmp_name_rec->units= NclMalloc(strlen("unknown") + 1);
					sprintf(tmp_name_rec->units,"unknown");
					name_rec = tmp_name_rec;
					i = -1;
				}

				if((name_rec != NULL)&&(grib_rec != NULL)){
					if (i < 0) {
						grib_rec->ptable_rec = NULL;
					}
					else {
						grib_rec->ptable_rec = name_rec;
					}
						



					grib_rec->level_indicator = (int)grib_rec->pds[9];
					_GetLevels(&grib_rec->level0,&grib_rec->level1,(int)grib_rec->pds[9],&(grib_rec->pds[10]));
					grib_rec->is_ensemble = 0;
					memset(&grib_rec->ens,0,sizeof(ENS));
					/* check for ensemble dimension */
					if ((center == 98 || grib_rec->eff_center == 98) && grib_rec->pds_size > 50) {
						switch ((int) grib_rec->pds[40]) {
						case 1:
							if (CnvtToDecimal(2,&(grib_rec->pds[43])) == 1035) {
								grib_rec->is_ensemble = 1;
								grib_rec->ens.extension_type = grib_rec->pds[40];
								grib_rec->ens.type = grib_rec->pds[42];
								grib_rec->ens.id = grib_rec->pds[49];
							}
							break;
						case 18:
						case 26:
						case 27:
						case 30:
						case 50:
							grib_rec->is_ensemble = 1;
							grib_rec->ens.id = grib_rec->pds[49];
							grib_rec->ens.extension_type = grib_rec->pds[40];
							break;
						case 15:
						case 16:
						case 22:
						case 23:
							grib_rec->is_ensemble = 1;
							grib_rec->ens.extension_type = grib_rec->pds[40];
							grib_rec->ens.id = CnvtToDecimal(2,&(grib_rec->pds[49]));
							break;
						}
					}
					else if (subcenter == 2 && grib_rec->pds_size > 44 && grib_rec->pds[40] == 1) {
						/* NCEP ensemble */
						grib_rec->is_ensemble = 1;
						grib_rec->ens.extension_type = 0;
						grib_rec->ens.type = grib_rec->pds[41];
						if (grib_rec->ens.type != 5) {
							grib_rec->ens.id = grib_rec->pds[42];
							grib_rec->ens.prod_id = grib_rec->pds[43];
						}
						else {
							TBLE2 *ptable;
							int ptable_count;

							grib_rec->ens.prob_type = 0;
							grib_rec->ens.lower_prob = 0;
							grib_rec->ens.upper_prob = 0;
							if (grib_rec->pds_size >= 47 &&
							    (grib_rec->param_number == 191 || grib_rec->param_number == 192)) {
								/* a Probability  product */

								ptable = &ncep_opn_params[0];
								ptable_count = sizeof(ncep_opn_params)/sizeof(TBLE2);
								grib_rec->ens.prob_param = NULL;
								for (i = 0; i < ptable_count; i++) {
									if (ptable[i].num == grib_rec->pds[45]) {
										grib_rec->ens.prob_param = ptable + i;
										break;
									}
								}
								if (grib_rec->pds_size >= 47)
									grib_rec->ens.prob_type = grib_rec->pds[46];
								if (grib_rec->pds_size >= 51)
									grib_rec->ens.lower_prob = 
										bytes2float(&(grib_rec->pds[47]));
								if (grib_rec->pds_size >= 54)
									grib_rec->ens.upper_prob = 
										bytes2float(&(grib_rec->pds[51]));
								if (grib_rec->pds_size >= 61)
									grib_rec->ens.n_members = grib_rec->pds[60];
							}
								
						}
					}

					if (strlen(name_rec->abrev) > 0) {
						strcpy((char*)buffer,name_rec->abrev);
					}
					else {
						sprintf((char*)buffer,"VAR_%d",grib_rec->param_number);
					}
					if (grib_rec->ens.prob_param) {
						sprintf((char*)&(buffer[strlen((char*)buffer)]),"_%s",grib_rec->ens.prob_param->abrev);
					}
					if((grib_rec->has_gds)&&(grib_rec->grid_number == 255 || grib_rec->grid_number == 0)) {
						sprintf((char*)&(buffer[strlen((char*)buffer)]),"_GDS%d",grib_rec->gds_type);
 					} else {
						sprintf((char*)&(buffer[strlen((char*)buffer)]),"_%d",grib_rec->grid_number);
					}
					for(i = 0; i < sizeof(level_index)/sizeof(int); i++) {
						if(level_index[i] == (int)grib_rec->pds[9]) { 
							break;
						}
					}
					if(i < sizeof(level_index)/sizeof(int)) {
						sprintf((char*)&(buffer[strlen((char*)buffer)]),"_%s",level_str[i]);
					} else {
						if(((int)grib_rec->pds[9]) != 0) {
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_%d",(int)grib_rec->pds[9]);
						}
					}
					grib_rec->time_period = 0;
					suffix = (int)(therec->options[GRIB_TIME_PERIOD_SUFFIX_OPT].values);
					switch((int)grib_rec->pds[20]) {
						char tpbuf[16];
					case 0:
					case 1:
					case 2:
						break;	
					case 3:
						grib_rec->time_period = (int)grib_rec->pds[19] - (int) grib_rec->pds[18];
						if (grib_rec->time_period == 0)
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_ave");
						else {
							grib_rec->time_period = AdjustedTimePeriod
								(grib_rec,grib_rec->time_period,grib_rec->pds[17],tpbuf);
							if (! suffix) tpbuf[0] = '\0';
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_ave%s",tpbuf);
						}
						break;
					case 4:
						grib_rec->time_period = (int)grib_rec->pds[19] - (int) grib_rec->pds[18];
						if (grib_rec->time_period == 0)
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_acc");
						else {
							grib_rec->time_period = AdjustedTimePeriod
								(grib_rec,grib_rec->time_period,grib_rec->pds[17],tpbuf);
							if (! suffix) tpbuf[0] = '\0';
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_acc%s",tpbuf);
						}
						break;
					case 5:
						grib_rec->time_period = (int)grib_rec->pds[19] - (int) grib_rec->pds[18];
						if (grib_rec->time_period == 0)
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_dif");
						else {
							grib_rec->time_period = AdjustedTimePeriod
								(grib_rec,grib_rec->time_period,grib_rec->pds[17],tpbuf);
							if (! suffix) tpbuf[0] = '\0';
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_dif%s",tpbuf);
						}
						break;
					case 6:
						grib_rec->time_period = (int)grib_rec->pds[19] - (int) grib_rec->pds[18];
						if (grib_rec->time_period == 0)
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_ave");
						else {
							grib_rec->time_period = AdjustedTimePeriod
								(grib_rec,grib_rec->time_period,grib_rec->pds[17],tpbuf);
							if (! suffix) tpbuf[0] = '\0';
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_ave%s",tpbuf);
						}
						break;
					case 7:
						/* average ref time - P1 to ref_time + P2 -- add 1 for extent of ref time unit itself */
						grib_rec->time_period = (int)grib_rec->pds[19] + (int) grib_rec->pds[18] + 1; 
						if (grib_rec->time_period == 0)
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_ave");
						else {
							grib_rec->time_period = AdjustedTimePeriod
								(grib_rec,grib_rec->time_period,grib_rec->pds[17],tpbuf);
							if (! suffix) tpbuf[0] = '\0';
							sprintf((char*)&(buffer[strlen((char*)buffer)]),"_ave%s",tpbuf);
						}
						break;
					default:
						sprintf((char*)&(buffer[strlen((char*)buffer)]),"_%d",(int)grib_rec->pds[20]);
						break;
					}

					grib_rec->var_name = (char*)NclMalloc((unsigned)strlen((char*)buffer) + 1);
					strcpy(grib_rec->var_name,(char*)buffer);
					grib_rec->var_name_q = NrmStringToQuark(grib_rec->var_name);
					grib_rec->long_name_q = NrmStringToQuark(name_rec->long_name);
					grib_rec->units_q = NrmStringToQuark(name_rec->units);

					if(therec->var_list == NULL) {
						therec->var_list = _NewListNode(grib_rec);
						therec->n_vars = 1;
					} else {
 					    step = therec->var_list;

					     if(!_FirstCheck(therec,step,grib_rec)) {
/*
* Keep in inorder list
*/
						while((step->next != NULL) && ParamCompare(step->next,grib_rec) < 0) {
							step = step->next;
						}
			
						if((step->next == NULL) 
						   ||(ParamCompare(step->next,grib_rec) > 0)) {
							/*
							 * No current instance of grib_rec->param_number insert immediately
							 */

							step2 = _NewListNode(grib_rec);
							_InsertNodeAfter(step,step2);
							therec->n_vars++;
						} else if(GridCompare(step->next,grib_rec) <= 0) {
							/*
							 * At this point param_number found and step->next points to first occurance of param_number
							 */
							while((step->next != NULL)
							      &&(ParamCompare(step->next,grib_rec) == 0)
							      &&(GridCompare(step->next,grib_rec) < 0)) {
								step = step->next;
							}
							if((step->next == NULL)
							   ||(ParamCompare(step->next,grib_rec) != 0)
							   ||(GridCompare(step->next,grib_rec) > 0)) {
								step2 = _NewListNode(grib_rec);
								_InsertNodeAfter(step,step2);
								therec->n_vars++;
							} else if(_CompareTimePeriod(step->next,grib_rec) <= 0) {
									while((step->next != NULL) 
									      &&(ParamCompare(step->next,grib_rec) == 0)
									      &&(GridCompare(step->next, grib_rec) == 0)
									      &&(_CompareTimePeriod(step->next,grib_rec) < 0)){
										step = step->next;
									}
									if((step->next == NULL)
									   ||(ParamCompare(step->next,grib_rec) != 0)
									   ||(GridCompare(step->next, grib_rec) != 0)
									   ||(_CompareTimePeriod(step->next,grib_rec) > 0)) {
										step2 = _NewListNode(grib_rec);	
										_InsertNodeAfter(step,step2);
										therec->n_vars++;
									} else if(step->next->level_indicator <= grib_rec->level_indicator) {
										while((step->next != NULL) 
										      &&(ParamCompare(step->next,grib_rec) == 0)
										      &&(GridCompare(step->next, grib_rec) == 0)
										      &&(_CompareTimePeriod(step->next,grib_rec) == 0)
										      &&(step->next->level_indicator < grib_rec->level_indicator)){
											step = step->next;
										}
										if((step->next == NULL)
										   ||(ParamCompare(step->next,grib_rec) != 0)
										   ||(GridCompare(step->next, grib_rec) != 0)
										   ||(_CompareTimePeriod(step->next,grib_rec) != 0)
										   ||(step->next->level_indicator > grib_rec->level_indicator)) {
											step2 = _NewListNode(grib_rec);	
											_InsertNodeAfter(step,step2);
											therec->n_vars++;
											
										} else {
											/*
											 * Att this point it falls through because 
											 * param_number, grid_number, time_period and level_indicator 
											 * are equal so its time to add the record
											 */
											_AddRecordToNode(step->next,grib_rec);
										}
									} else {
										step2 = _NewListNode(grib_rec);
										_InsertNodeAfter(step,step2);
										therec->n_vars++;
									}
								} else {
									step2 = _NewListNode(grib_rec);
									_InsertNodeAfter(step,step2);
									therec->n_vars++;
								}
						} else {
							step2 = _NewListNode(grib_rec);
							_InsertNodeAfter(step,step2);
							therec->n_vars++;
						}
					}
				}
			}
			if(tmp_name_rec != NULL) {
				NclFree(tmp_name_rec->abrev);	
				NclFree(tmp_name_rec->units);
				NclFree(tmp_name_rec->long_name);
				NclFree(tmp_name_rec);
				tmp_name_rec = NULL;
			}
		} else if(ret==GRIBERROR){
			error_count++;
			if(error_count > 1000) {
				NhlPError(NhlFATAL, NhlEUNKNOWN, "NclGRIB: More than 1000 incomplete records were found, grib file appears to be corrupted, make sure it is not a tar file or a COS blocked grib file.");
				return(NULL);
			} else {
				NhlPError(NhlWARNING, NhlEUNKNOWN, "NclGRIB: Detected incomplete record, skipping record");
			}
		}
		offset = nextoff;
		grib_rec = NULL;
	}
	if(therec != NULL ) {
		therec->grib_grid_cache = NULL;
/*
* Next step is to sort by time and then level each of the variables in the list
*/
		step = therec->var_list;
		k = 0;
		step2 = NULL;
		while(step != NULL) {
			grib_rec_list = step->thelist;


			sortar = (GribRecordInqRecList**)NclMalloc((unsigned)sizeof(GribRecordInqRecList*)*step->n_entries);
			i = 0;	
/*
* Scan through records and compute time offset from top of the grib record. 
* All offsets based time_units_indicator of top of the grib parameter record
* First determine an offset in time units based on time_units_indicator and time_range_indicator
* then determine offset in same units from the top of the parameter list's time reference 
*/		

			if (step->variable_time_unit) {
				while(grib_rec_list != NULL) {
					sortar[i] = grib_rec_list;
					grib_rec_list->rec_inq->time_offset = _GetConvertedTimeOffset(
						step->time_unit_indicator,
						(int) grib_rec_list->rec_inq->pds[17],
						(int)grib_rec_list->rec_inq->pds[20],
						&(grib_rec_list->rec_inq->pds[18]));
					grib_rec_list = grib_rec_list->next;
					i++;
				}
			}
			else {
				while(grib_rec_list != NULL) {
					sortar[i] = grib_rec_list;
					grib_rec_list->rec_inq->time_offset = _GetTimeOffset(
						(int)grib_rec_list->rec_inq->pds[20],
						&(grib_rec_list->rec_inq->pds[18]));
					grib_rec_list = grib_rec_list->next;
					i++;
				}
			}
			qsort((void*)sortar,i,sizeof(GribRecordInqRecList*),record_comp);

#if 0
		
			j = 0;
			i = 0;
			l = 0;
			ptr = sortar;
			start_ptr = sortar;
			while(j < step->n_entries) {
				start_ptr = ptr;
				i = 0;
				while((j< step->n_entries)&&(date_comp((void*)&(ptr[i]),(void*)start_ptr))==0) {
					i++;
					j++;
				}
				qsort((void*)ptr,i,sizeof(GribRecordInqRecList*),level_comp);
#if 0
				if((*ptr)->rec_inq->level0 != -1) {
					for(k = 0; k < i-1; k++) {
						if(!level_comp((void*)&(ptr[k]),(void*)&(ptr[k+1]))) {
							NhlPError(NhlWARNING,NhlEUNKNOWN,"NclGRIB: Duplicate GRIB record found, skipping record");
							ptr[k] = ptr[k+1];
							l++;

						}
					}
				}
#endif
				if(j < step->n_entries) {
					ptr = &(ptr[i]);
				}
			}

#endif





/* 
* This is temporary code to print out whats going on
*/	
			step->thelist = sortar[0];
			for(i = 0; i < step->n_entries - 1; i++) {
				sortar[i]->next = sortar[i+1];
			} 
			sortar[step->n_entries - 1]->next = NULL;
/*
* Next step is to determine dimensionality for the variable.
* Dimensionality is detemined [yy:mm:dd:hh:mm] x [forcast offset (P1)] x [levels] x [grid x] x [grid y]
* k is the variable number, step points to GribParamList, and sortar has all elments in order and 
* connected. Missing entrys will be inserted when it is determined that levels or forcast times are missing
*/
/*
* Need to reassign everything incase a Duplicate record was found
*/

/*
			fprintf(stdout,"param# = %d\t%d\t%s\n",step->param_number,step->grid_number,step->thelist->rec_inq->var_name);
*/


/*
* Determine grid and coordinate information as well as dimenionality foreach record
* Also fills in missing values
*/
			if((step->has_gds)&&(step->gds_type==50)) {
				step->var_info.doff = 2;
				retvalue = _DetermineDimensionAndGridInfo(therec,step);
			} else {
				step->var_info.doff = 1;
				retvalue = _DetermineDimensionAndGridInfo(therec,step);
			}
			if((retvalue < NhlNOERROR)&&(step2 == NULL)) {
				step = step->next;
				step2 = therec->var_list; 
				therec->var_list = step;
				therec->n_vars--;
				_GribFreeParamRec(step2);
				step2 = NULL;
				
			} else if(retvalue < NhlNOERROR) {
				tmpstep = step;
				step2->next = step->next;
				step = step->next;
				therec->n_vars--;
				_GribFreeParamRec(tmpstep);
			} else {
				step2 = step;	
				step = step->next;
				k++;
			}
			NclFree(sortar);
			sortar = NULL;
		}
/*
* Now its time to scan variables and detemine all dimensions in this file and combine 
* dimensions that are equal. The last two dimensions will always be the grid dimensions
* Variables can be two dimensions to five. The first three dimensions are 
* initial_time x forcast offset x levels. They'll always be in that order but
* each dimension could be 1 in which case it isn't a real dimension but an attribute
*/
			_SetFileDimsAndCoordVars(therec);
			_SetAttributeLists(therec);
			_MakeVarnamesUnique(therec);
			if ((int)(therec->options[GRIB_PRINT_RECORD_INFO_OPT].values) != 0) {
				_PrintRecordInfo(therec);
			}

#if 0
			/* this is for debugging */
			{
				GribParamList *step;
				int rec_count = 0, rec_count1 = 0;
				int num_recs;
				int n;
				GribRecordInqRecList *recl;
				int *recs;
				int q,r;
				recs = NhlMalloc(sizeof(int) * rec_num);
				for (step = therec->var_list; step != NULL; step = step->next) {
					num_recs = 1;
					if (step->var_info.num_dimensions > 2) {
						for (n = step->var_info.num_dimensions - 3; n > -1; n--) {
							num_recs *= step->var_info.dim_sizes[n];
						}
					}
					rec_count += num_recs;
					for (n = 0; n < step->n_entries; n++) {
						recl = &step->thelist[n];
						if (! recl->rec_inq)
							continue;
						recs[rec_count1] = recl->rec_inq->rec_num;
						printf("%d\t%s\n",recl->rec_inq->rec_num,
						       NrmQuarkToString(step->var_info.var_name_quark));
						rec_count1++;
					}
				}
				printf ("there are %d or %d recordsin %d variables\n",rec_count,rec_count1,therec->n_vars);
				for (q = 1; q < rec_num + 1; q++) {
					for (r = 0; r < rec_count1; r++) {
						if (recs[r] != q)
							continue;
						break;
					}
					if (r == rec_count)
						printf ("record %d not found\n",q);
				}
				NhlFree(recs);
				
			}
#endif
			close(fd);	
			NclFree(vbuf);

			return(therec);
		} 
	}
	if (fd > 0) {
		NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Could not open (%s) no grib records found",NrmQuarkToString(path));
	} else {
		NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Could not open (%s) check permissions",NrmQuarkToString(path));
	}
	return(NULL);
}

static void *GribCreateFile
#if	NhlNeedProto
(void *rec,NclQuark path)
#else
(rec,path)
void *rec;
NclQuark path;
#endif
{
NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Grib files can only be read, Grib file can not be created using NCL");
return(NULL);
}

static void GribFreeFileRec
#if	NhlNeedProto
(void* therec)
#else
(therec)
void *therec;
#endif
{
	GribFileRecord *thefile = (GribFileRecord*)therec;
	GribParamList *vstep,*vstep1;
	GribDimInqRecList *dim,*dim1;
	GribInternalVarList *ivars,*itmp;
	GribAttInqRecList *theatts,*tmp;
	NclGribCacheList *thelist,*thelist0;
	NclGribCacheRec *ctmp,*ctmp0;

	vstep = thefile->var_list;
	while(vstep != NULL){
		vstep1 = vstep->next;
		_GribFreeParamRec(vstep);
		vstep  = vstep1;
	}
	thelist = thefile->grib_grid_cache;
        while(thelist != NULL) {
		if (thelist->int_missing_rec) {
			_NclDestroyObj((NclObj)thelist->int_missing_rec);
		}
		if (thelist->float_missing_rec) {
			_NclDestroyObj((NclObj)thelist->float_missing_rec);
		}
		ctmp = thelist->thelist;
		while(ctmp!=NULL) {	
			ctmp0 = ctmp;
			ctmp = ctmp->next;
			NclFree(ctmp0);
		}
		thelist0 = thelist;
		thelist = thelist->next;
		NclFree(thelist0);
	}

	ivars = thefile->internal_var_list;
	while(ivars != NULL) {
		_NclDestroyObj((NclObj)ivars->int_var->value);
		theatts = ivars->int_var->theatts;
		while(theatts != NULL) {
			_NclDestroyObj((NclObj)theatts->att_inq->thevalue);
			NclFree(theatts->att_inq);
			tmp = theatts;
			theatts = theatts->next;
			NclFree(tmp);
		}	
		NclFree(ivars->int_var);
		itmp = ivars;	
		ivars = ivars->next;
		NclFree(itmp);
	}
	dim = thefile->it_dims;
	if(dim != NULL) {
		while(dim != NULL) {
			dim1 = dim->next;
			if(dim->dim_inq != NULL) {
				NclFree(dim->dim_inq);
			}
			NclFree(dim);
			dim = dim1;
		}
	}
	dim = thefile->ft_dims;
	if(dim != NULL) {
		while(dim != NULL) {
			dim1 = dim->next;
			if(dim->dim_inq != NULL) {
				NclFree(dim->dim_inq);
			}
			NclFree(dim);
			dim = dim1;
		}
	}
	dim = thefile->lv_dims;
	if(dim != NULL) {
		while(dim != NULL) {
			dim1 = dim->next;
			if(dim->dim_inq != NULL) {
				NclFree(dim->dim_inq);
			}
			NclFree(dim);
			dim = dim1;
		}
	}
	dim = thefile->grid_dims;
	if(dim != NULL) {
		while(dim != NULL) {
			dim1 = dim->next;
			if(dim->dim_inq != NULL) {
				if(dim->dim_inq->gds != NULL) {
					NclFree(dim->dim_inq->gds);
				}
				NclFree(dim->dim_inq);
			}
			NclFree(dim);
			dim = dim1;
		}
	}
	dim = thefile->scalar_dims;
	if(dim != NULL) {
		while(dim != NULL) {
			dim1 = dim->next;
			if(dim->dim_inq != NULL) {
				NclFree(dim->dim_inq);
			}
			NclFree(dim);
			dim = dim1;
		}
	}
	if (thefile->options) {
		NclFree(thefile->options);
	}
	NclFree(therec);
}

static NclQuark* GribGetVarNames
#if	NhlNeedProto
(void* therec, int *num_vars)
#else
(therec, num_vars)
void* therec;
int *num_vars;
#endif
{
	GribFileRecord *thefile = (GribFileRecord*)therec;
	GribParamList *step;
	GribInternalVarList *vstep;
	int i;
	NclQuark *arout;

	*num_vars = thefile->n_vars + thefile->n_internal_vars;
	arout = (NclQuark*)NclMalloc((unsigned)sizeof(NclQuark)* *num_vars);


	step = thefile->var_list;	
	for(i = 0; i < thefile->n_vars; i++) {
		arout[i] = step->var_info.var_name_quark;
		step = step->next;
	}

	vstep = thefile->internal_var_list;
	for(; i < thefile->n_vars + thefile->n_internal_vars; i++) {
		arout[i] = vstep->int_var->var_info.var_name_quark;
		vstep = vstep->next;
	}
	return(arout);
}

static NclFVarRec *GribGetVarInfo
#if	NhlNeedProto
(void *therec, NclQuark var_name)
#else
(therec, var_name)
void *therec;
NclQuark var_name;
#endif
{
GribFileRecord *thefile = (GribFileRecord*)therec;
GribParamList *step;
NclFVarRec *tmp;
GribInternalVarList *vstep;
int i;

vstep = thefile->internal_var_list;
while(vstep != NULL) {
	if(vstep->int_var->var_info.var_name_quark == var_name) {
		tmp = (NclFVarRec*)NclMalloc(sizeof(NclFVarRec));
		tmp->var_name_quark  = vstep->int_var->var_info.var_name_quark;
		tmp->var_full_name_quark  = vstep->int_var->var_info.var_name_quark;
		tmp->var_real_name_quark  = vstep->int_var->var_info.var_name_quark;
		tmp->data_type  = vstep->int_var->var_info.data_type;
		tmp->num_dimensions  = vstep->int_var->var_info.num_dimensions;
		for(i=0;i< tmp->num_dimensions;i++) {
			tmp->file_dim_num[i]  = vstep->int_var->var_info.file_dim_num[i];
		}
		return(tmp);
	} else {
		vstep = vstep->next;
	}
}	

step = thefile->var_list;	
while(step != NULL) {
	if(step->var_info.var_name_quark == var_name) {
		tmp = (NclFVarRec*)NclMalloc(sizeof(NclFVarRec));
		tmp->var_name_quark  = step->var_info.var_name_quark;
		tmp->var_full_name_quark  = step->var_info.var_name_quark;
		tmp->var_real_name_quark  = step->var_info.var_name_quark;
		tmp->data_type  = step->var_info.data_type;
		tmp->num_dimensions  = step->var_info.num_dimensions;
		for(i=0;i< tmp->num_dimensions;i++) {
			tmp->file_dim_num[i]  = step->var_info.file_dim_num[i];
		}
		return(tmp);
	} else {
		step = step->next;
	}
}
return(NULL);
}

static NclQuark *GribGetDimNames
#if	NhlNeedProto
(void* therec, int* num_dims)
#else
(therec,num_dims)
void *therec;
int *num_dims;
#endif
{
GribFileRecord *thefile = (GribFileRecord*)therec;
GribDimInqRecList *dstep;
NclQuark *dims;
int i,j;

dims = (NclQuark*)NclMalloc((unsigned)sizeof(NclQuark)*thefile->total_dims);
i = 0;
*num_dims = thefile->total_dims;
dstep = thefile->scalar_dims;
for(j=0; j < thefile->n_scalar_dims; j++) {
	dims[dstep->dim_inq->dim_number] = dstep->dim_inq->dim_name;	
	dstep = dstep->next;
}
dstep = thefile->ensemble_dims;
for(j=0; j < thefile->n_ensemble_dims; j++) {
	dims[dstep->dim_inq->dim_number] = dstep->dim_inq->dim_name;	
	dstep = dstep->next;
}
dstep = thefile->it_dims;
for(j=0; j < thefile->n_it_dims; j++) {
	dims[dstep->dim_inq->dim_number] = dstep->dim_inq->dim_name;	
	dstep = dstep->next;
}
dstep = thefile->ft_dims;
for(j=0; j < thefile->n_ft_dims; j++) {
	dims[dstep->dim_inq->dim_number] = dstep->dim_inq->dim_name;	
	dstep = dstep->next;
}
dstep = thefile->lv_dims;
for(j=0; j < thefile->n_lv_dims; j++) {
	dims[dstep->dim_inq->dim_number] = dstep->dim_inq->dim_name;	
	dstep = dstep->next;
}
dstep = thefile->grid_dims;
for(j=0; j < thefile->n_grid_dims; j++) {
	dims[dstep->dim_inq->dim_number] = dstep->dim_inq->dim_name;	
	dstep = dstep->next;
}

return(dims);
}

static NclFDimRec *GribGetDimInfo
#if	NhlNeedProto
(void* therec, NclQuark dim_name_q)
#else
(therec,dim_name_q)
void* therec;
NclQuark dim_name_q;
#endif
{
GribFileRecord *thefile = (GribFileRecord*)therec;
GribDimInqRecList *dstep;
NclFDimRec *tmpd = NULL;
char *tmp;

tmp = NrmQuarkToString(dim_name_q);
/*
* first character is either i,f, g or l
*/
	dstep = thefile->scalar_dims;
	while(dstep != NULL) {
		if(dstep->dim_inq->dim_name == dim_name_q) {
			tmpd = (NclFDimRec*)NclMalloc(sizeof(NclFDimRec));
			tmpd->dim_name_quark = dim_name_q;
			tmpd->dim_size = dstep->dim_inq->size;
			tmpd->is_unlimited = 0;
			return(tmpd);
		}
		dstep = dstep->next;
	}		
	dstep = thefile->ensemble_dims;
	while(dstep != NULL) {
		if(dstep->dim_inq->dim_name == dim_name_q) {
			tmpd = (NclFDimRec*)NclMalloc(sizeof(NclFDimRec));
			tmpd->dim_name_quark = dim_name_q;
			tmpd->dim_size = dstep->dim_inq->size;
			tmpd->is_unlimited = 0;
			return(tmpd);
		}
		dstep = dstep->next;
	}		
	dstep = thefile->it_dims;
	while(dstep != NULL) {
		if(dstep->dim_inq->dim_name == dim_name_q) {
			tmpd = (NclFDimRec*)NclMalloc(sizeof(NclFDimRec));
			tmpd->dim_name_quark = dim_name_q;
			tmpd->dim_size = dstep->dim_inq->size;
			tmpd->is_unlimited = 0;
			return(tmpd);
		}
		dstep = dstep->next;
	}		
	dstep = thefile->ft_dims;
	while(dstep != NULL) {
		if(dstep->dim_inq->dim_name == dim_name_q) {
			tmpd = (NclFDimRec*)NclMalloc(sizeof(NclFDimRec));
			tmpd->dim_name_quark = dim_name_q;
			tmpd->dim_size = dstep->dim_inq->size;
			tmpd->is_unlimited = 0;
			return(tmpd);
		}
		dstep = dstep->next;
	}		
	dstep = thefile->grid_dims;
	while(dstep != NULL) {
		if(dstep->dim_inq->dim_name == dim_name_q) {
			tmpd = (NclFDimRec*)NclMalloc(sizeof(NclFDimRec));
			tmpd->dim_name_quark = dim_name_q;
			tmpd->dim_size = dstep->dim_inq->size;
			tmpd->is_unlimited = 0;
			return(tmpd);
		}
		dstep = dstep->next;
	}		
	dstep = thefile->lv_dims;
	while(dstep != NULL) {
		if(dstep->dim_inq->dim_name == dim_name_q) {
			tmpd = (NclFDimRec*)NclMalloc(sizeof(NclFDimRec));
			tmpd->dim_name_quark = dim_name_q;
			tmpd->dim_size = dstep->dim_inq->size;
			tmpd->is_unlimited = 0;
			return(tmpd);
		}
		dstep = dstep->next;
	}		
	return(NULL);
}

static void *GribReadVar
#if	NhlNeedProto
(void* therec, NclQuark thevar, long* start, long* finish,long* stride,void* storage)
#else
(therec, thevar, start, finish,stride,storage)
void* therec;
NclQuark thevar;
long* start;
long* finish;
long* stride;
void* storage;
#endif
{
	GribFileRecord *rec = (GribFileRecord*)therec;
	GribParamList *step;
	GribRecordInqRec *current_rec;
	void *out_data;
	long *grid_start;
	long *grid_finish;
	long *grid_stride;
	int n_other_dims = 0;
	int current_index[5] = {0,0,0,0,0};
	int dim_offsets[5] = {-1,-1,-1,-1,-1};
	int i,j;
	int offset;
	int done = 0,inc_done =0;
	int data_offset = 0;
	void *tmp;
	void *missing;
	NclScalar missingv;
	int int_or_float = 0;
	int fd;
	ng_size_t grid_dim_sizes[3];
	int n_grid_dims;
	NclMultiDValData tmp_md;
	NclSelectionRecord  sel_ptr;
	GribInternalVarList *vstep;
	int current_interp_method;

	vstep = rec->internal_var_list;
	while(vstep != NULL ) {
		if(vstep->int_var->var_info.var_name_quark == thevar) {
			sel_ptr.n_entries = vstep->int_var->var_info.num_dimensions;
			out_data = storage;
			for(i = 0; i < vstep->int_var->var_info.num_dimensions; i++ ) {
				sel_ptr.selection[i].sel_type = Ncl_SUBSCR;
				sel_ptr.selection[i].dim_num = i;
				sel_ptr.selection[i].u.sub.start = start[i];
				sel_ptr.selection[i].u.sub.finish = finish[i];
				sel_ptr.selection[i].u.sub.stride = stride[i];
				sel_ptr.selection[i].u.sub.is_single = 0;
			}
			tmp_md = (NclMultiDValData)_NclReadSubSection((NclData)vstep->int_var->value,&sel_ptr,NULL);
			memcpy((void*)&((char*)out_data)[data_offset],tmp_md->multidval.val,tmp_md->multidval.totalsize);
			if(tmp_md->obj.status != PERMANENT) {
				_NclDestroyObj((NclObj)tmp_md);
			}
			return(out_data);
		}
		vstep = vstep->next;

	}

	if ((NrmQuark)(rec->options[GRIB_THINNED_GRID_INTERPOLATION_OPT].values) == NrmStringToQuark("cubic")) {
		current_interp_method = 1;
	}
	else {
		current_interp_method = 0;
	}



	step = rec->var_list;
	while(step != NULL) {
		if(step->var_info.var_name_quark == thevar) {
			fd = open(NrmQuarkToString(rec->file_path_q),O_RDONLY);
			/*
			vbuf = (void*)NclMalloc(4*getpagesize());
			setvbuf(fd,vbuf,_IOFBF,4*getpagesize());
			*/

			out_data = storage;

			if(step->var_info.doff == 1) {
				grid_start = &(start[(step->var_info.num_dimensions - 2) ]);
				grid_finish = &(finish[(step->var_info.num_dimensions - 2) ]);
				grid_stride = &(stride[(step->var_info.num_dimensions - 2) ]);
				n_other_dims = step->var_info.num_dimensions - 2;
				
			
				for(i = 0; i < n_other_dims; i++) {
					current_index[i] = start[i];
					dim_offsets[i] = step->var_info.dim_sizes[i];
					for (j = i + 1; j < n_other_dims; j++) {
						dim_offsets[i] *= step->var_info.dim_sizes[j];
					}
				}
				n_grid_dims = 2;
				grid_dim_sizes[0] = step->var_info.dim_sizes[step->var_info.num_dimensions - 2];
				grid_dim_sizes[1] = step->var_info.dim_sizes[step->var_info.num_dimensions - 1];
				sel_ptr.n_entries = 2;
				sel_ptr.selection[0].sel_type = Ncl_SUBSCR;
				sel_ptr.selection[0].dim_num = 0;
				sel_ptr.selection[0].u.sub.start = grid_start[0];
				sel_ptr.selection[0].u.sub.finish = grid_finish[0];
				sel_ptr.selection[0].u.sub.stride = grid_stride[0];
				sel_ptr.selection[0].u.sub.is_single = 0;
				sel_ptr.selection[1].sel_type = Ncl_SUBSCR;
				sel_ptr.selection[1].dim_num = 1;
				sel_ptr.selection[1].u.sub.start = grid_start[1];
				sel_ptr.selection[1].u.sub.finish = grid_finish[1];
				sel_ptr.selection[1].u.sub.stride = grid_stride[1];
				sel_ptr.selection[1].u.sub.is_single = 0;
			} else if(step->var_info.doff == 2) {
				grid_start = &(start[(step->var_info.num_dimensions - 3) ]);
				grid_finish = &(finish[(step->var_info.num_dimensions - 3) ]);
				grid_stride = &(stride[(step->var_info.num_dimensions - 3) ]);
				n_other_dims = step->var_info.num_dimensions - 3;
				
			
				for(i = 0; i < n_other_dims; i++) {
					current_index[i] = start[i];
					dim_offsets[i] = step->var_info.dim_sizes[i];
					for (j = i + 1; j < n_other_dims; j++) {
						dim_offsets[i] *= step->var_info.dim_sizes[j];
					}
				}
				n_grid_dims = 3;
				grid_dim_sizes[0] = step->var_info.dim_sizes[step->var_info.num_dimensions - 3];
				grid_dim_sizes[1] = step->var_info.dim_sizes[step->var_info.num_dimensions - 2];
				grid_dim_sizes[2] = step->var_info.dim_sizes[step->var_info.num_dimensions - 1];
				sel_ptr.n_entries = 3;
				sel_ptr.selection[0].sel_type = Ncl_SUBSCR;
				sel_ptr.selection[0].dim_num = 0;
				sel_ptr.selection[0].u.sub.start = grid_start[0];
				sel_ptr.selection[0].u.sub.finish = grid_finish[0];
				sel_ptr.selection[0].u.sub.stride = grid_stride[0];
				sel_ptr.selection[0].u.sub.is_single = 0;
				sel_ptr.selection[1].sel_type = Ncl_SUBSCR;
				sel_ptr.selection[1].dim_num = 1;
				sel_ptr.selection[1].u.sub.start = grid_start[1];
				sel_ptr.selection[1].u.sub.finish = grid_finish[1];
				sel_ptr.selection[1].u.sub.stride = grid_stride[1];
				sel_ptr.selection[1].u.sub.is_single = 0;
				sel_ptr.selection[2].sel_type = Ncl_SUBSCR;
				sel_ptr.selection[2].dim_num = 2;
				sel_ptr.selection[2].u.sub.start = grid_start[2];
				sel_ptr.selection[2].u.sub.finish = grid_finish[2];
				sel_ptr.selection[2].u.sub.stride = grid_stride[2];
				sel_ptr.selection[2].u.sub.is_single = 0;
			}
			

			offset = 0;
			while(!done) {
				offset = 0;
				if(n_other_dims > 0 ) {
					for(i = 0; i < n_other_dims - 1; i++) {
						offset += dim_offsets[i+1] * current_index[i];
					}
					offset += current_index[n_other_dims-1];
				}
				current_rec = step->thelist[offset].rec_inq;
	/*
	* For now(4/27/98) missing records persist, Eventually I'll implement one missing record per grid type for
	* general use. (8/13/07: now the data is shared although the records are still created -- dib)
	*/
				if(current_rec == NULL) {
					step->thelist[offset].rec_inq = _MakeMissingRec();
                                        current_rec = step->thelist[offset].rec_inq;
                                        current_rec->the_dat = _GetCacheMissingVal(therec,step,current_rec);
                                }
				
				if((current_rec->the_dat == NULL) || 
				   (current_rec->interp_method != current_interp_method &&
				    current_rec->var_name_q > NrmNULLQUARK)) {
	/*
	* Retrieves LRU cache MultiDVal specific to this grid type
	*/
					if (current_rec->the_dat) {
						current_rec->interp_method = current_interp_method;
					}
					else {
						current_rec->the_dat = _GetCacheVal(therec,step,current_rec);
						if(current_rec->the_dat == NULL){
							NhlPError(NhlFATAL,NhlEUNKNOWN,
								  "NclGRIB: Unrecoverable caching error reading variable %s; can't continue",current_rec->var_name);
							close(fd);
							/*
							NclFree(vbuf);
							*/
							return(NULL);
						}
					}
	/*
	* grid and grid_gds will overwrite tmp
	*/
					tmp = current_rec->the_dat->multidval.val;
					if((current_rec->has_gds) &&
					   (current_rec->grid_number == 255 || current_rec->grid_number == 0) &&
					   (current_rec->grid_gds_tbl_index > -1)) {
						if(grid_gds[current_rec->grid_gds_tbl_index].un_pack != NULL) {
							int_or_float = (*grid_gds[current_rec->grid_gds_tbl_index].un_pack)
								(fd,&tmp,&missing,current_rec,step);
						}
					} else if(current_rec->grid_tbl_index > -1 && (grid[current_rec->grid_tbl_index].un_pack != NULL)) {
						int_or_float = (*grid[current_rec->grid_tbl_index].un_pack)
							(fd,&tmp,&missing,current_rec,step);
					} else if((current_rec->has_gds)&&(current_rec->grid_gds_tbl_index > -1)) {
						if(grid_gds[current_rec->grid_gds_tbl_index].un_pack != NULL) {
							int_or_float = (*grid_gds[current_rec->grid_gds_tbl_index].un_pack)
								(fd,&tmp,&missing,current_rec,step);
						}
					}
					if(tmp != NULL) {
						if(int_or_float) {
							if(missing != NULL) {
								missingv.intval = *(int*)missing;
							} else {
								missingv.intval = DEFAULT_MISSING_INT;
							}
	/*
	* Needed to fix chicken/egg problem with respect to type and missing values
	*/
							_AdjustCacheTypeAndMissing(int_or_float,current_rec->the_dat,(missing == NULL) ? NULL : &missingv);

							NclFree(missing);
						} else {
							if(missing != NULL) {
								missingv.floatval = *(float*)missing;
							} else {
								missingv.floatval = DEFAULT_MISSING_FLOAT;
							}
	/*
	* Needed to fix chicken/egg problem with respect to type and missing values
	*/
							_AdjustCacheTypeAndMissing(int_or_float,current_rec->the_dat,(missing == NULL) ? NULL : &missingv);

							NclFree(missing);
						}
					} else {
	/*
	* Need to figure out what to do here
	*/
					}
				} 
				if(current_rec->the_dat != NULL) {
					tmp_md = (NclMultiDValData)_NclReadSubSection((NclData)current_rec->the_dat,&sel_ptr,NULL);
					memcpy((void*)&((char*)out_data)[data_offset],tmp_md->multidval.val,tmp_md->multidval.totalsize);
					data_offset += tmp_md->multidval.totalsize;
					if(tmp_md->obj.status != PERMANENT) {
						_NclDestroyObj((NclObj)tmp_md);
					}
				} else {
					NhlPError(NhlFATAL,NhlEUNKNOWN,
						  "NclGRIB: Unrecoverable error reading variable %s; can't continue",
						  current_rec->var_name);
					close(fd);
					/*
					NclFree(vbuf);
					*/
					return(NULL);
				}

				if(n_other_dims > 0 ) {	
					current_index[n_other_dims-1] += stride[n_other_dims-1];
					for(i = n_other_dims-1; i > 0 ; i--) {
						if(current_index[i] > finish[i]) {
							current_index[i] = start[i];
							current_index[i-1] += stride[i-1];
						} else {
							inc_done = 1;
						}
						if(inc_done) {
							inc_done = 0;
							break;
						}
					}
					if(current_index[0] > finish[0]) {
						done = 1;
					}
				} else {
					done = 1;
				}
			}
			close(fd);
			/*
			NclFree(vbuf);
			*/
			return(out_data);
		} 
		step = step->next;
	}
	NhlPError(NhlFATAL,NhlEUNKNOWN,"NclGRIB: Variable (%s) is not an element of file (%s)",NrmQuarkToString(thevar),NrmQuarkToString(rec->file_path_q));

	return(NULL);
}

static NclFVarRec *GribGetCoordInfo
#if	NhlNeedProto
(void* therec, NclQuark thevar)
#else
(therec, thevar)
	void* therec;
	NclQuark thevar;
#endif
{
	return(GribGetVarInfo(therec, thevar));
}

static void *GribReadCoord
#if	NhlNeedProto
(void* therec, NclQuark thevar, long* start, long* finish,long* stride,void* storage)
#else
(therec, thevar, start, finish,stride,storage)
	void* therec;
	NclQuark thevar;
	long* start;
	long* finish;
	long* stride;
	void* storage;
#endif
{
	return(GribReadVar(therec, thevar, start, finish,stride,storage));
}

static NclQuark *GribGetAttNames
#if	NhlNeedProto
(void* therec,int *num_atts)
#else
(therec,num_atts)
	void* therec;
	int *num_atts;
#endif
{	
	*num_atts = 0;
	return(NULL);
}

static NclFAttRec* GribGetAttInfo
#if	NhlNeedProto
(void* therec, NclQuark att_name_q)
#else
(therec, att_name_q)
void* therec;
NclQuark att_name_q;
#endif
{
	return(NULL);
}

static NclQuark *GribGetVarAttNames
#if	NhlNeedProto
(void *therec , NclQuark thevar, int* num_atts)
#else
(therec , thevar, num_atts)
	void *therec;
	NclQuark thevar;
	int* num_atts;
#endif
{
	GribFileRecord *thefile = (GribFileRecord*)therec;
	GribParamList *step;
	GribInternalVarList *vstep;
	NclQuark *arout = NULL;
	GribAttInqRecList *theatts = NULL;
	int i;


	vstep = thefile->internal_var_list;
	while(vstep != NULL) {
		if(vstep->int_var->var_info.var_name_quark == thevar) {
			*num_atts = vstep->int_var->n_atts;
			arout = (NclQuark*)NclMalloc(sizeof(NclQuark)*vstep->int_var->n_atts);
			theatts = vstep->int_var->theatts;
			break;
		} else {
			vstep = vstep->next;
		}
	}	

	if(vstep == NULL ) {
		step = thefile->var_list;	
		while(step != NULL) {
			if(step->var_info.var_name_quark == thevar) {
				*num_atts = step->n_atts;
				arout = (NclQuark*)NclMalloc(sizeof(NclQuark)*step->n_atts);
				theatts = step->theatts;
				break;
			} else {
				step = step->next;
			}
		}
	}
	if((arout != NULL)&&(theatts!= NULL))  {
		for(i = 0; i < *num_atts; i++) {
			arout[i] = theatts->att_inq->name;
			theatts = theatts->next;
		}
		return(arout);
	} else {
		*num_atts = 0;	
		return(NULL);
	}
}
static NclFAttRec *GribGetVarAttInfo
#if	NhlNeedProto
(void *therec, NclQuark thevar, NclQuark theatt)
#else
(therec, thevar, theatt)
	void *therec;
	NclQuark thevar;
	NclQuark theatt;
#endif
{
	GribFileRecord *thefile = (GribFileRecord*)therec;
	GribParamList *step;
	GribInternalVarList *vstep;
	GribAttInqRecList *theatts = NULL;
	NclFAttRec *tmp;


	vstep = thefile->internal_var_list;
	while(vstep != NULL) {
		if(vstep->int_var->var_info.var_name_quark == thevar) {
			theatts = vstep->int_var->theatts;
			break;
		} else {
			vstep = vstep->next;
		}
	}	
	if(vstep == NULL ) {
		step = thefile->var_list;	
		while(step != NULL) {
			if(step->var_info.var_name_quark == thevar) {
				theatts = step->theatts;
				break;
			} else {
				step = step->next;
			}
		}
	}
	if(theatts!= NULL)  {
		while(theatts != NULL) {
			if(theatts->att_inq->name == theatt) {
				tmp = (NclFAttRec*)NclMalloc(sizeof(NclFAttRec));
				tmp->att_name_quark = theatt;
				tmp->data_type = theatts->att_inq->thevalue->multidval.data_type;
				tmp->num_elements = theatts->att_inq->thevalue->multidval.totalelements;
				return(tmp);
			}
			theatts = theatts->next;
		}
	} 
	return(NULL);
}


static void *GribReadVarAtt
#if	NhlNeedProto
(void * therec, NclQuark thevar, NclQuark theatt, void * storage)
#else
(therec, thevar, theatt, storage)
void * therec;
NclQuark thevar;
NclQuark theatt;
void* storage;
#endif
{
	GribFileRecord *thefile = (GribFileRecord*)therec;
	GribParamList *step;
	GribInternalVarList *vstep;
	GribAttInqRecList *theatts = NULL;
	void *out_dat;

	vstep = thefile->internal_var_list;
	while(vstep != NULL) {
		if(vstep->int_var->var_info.var_name_quark == thevar) {
			theatts = vstep->int_var->theatts;
			break;
		} else {
			vstep = vstep->next;
		}
	}	

	if(vstep == NULL ) {
		step = thefile->var_list;	
		while(step != NULL) {
			if(step->var_info.var_name_quark == thevar) {
				theatts = step->theatts;	
				break;
			} else {
				step = step->next;
			}
		}
	}
	if(theatts!= NULL)  {
		while(theatts != NULL) {
			if(theatts->att_inq->name == theatt) {
				if(storage != NULL) {
					memcpy(storage,theatts->att_inq->thevalue->multidval.val,theatts->att_inq->thevalue->multidval.totalsize);
					return(storage);
				} else {
					out_dat = (void*)NclMalloc(theatts->att_inq->thevalue->multidval.totalsize);
					memcpy(out_dat,theatts->att_inq->thevalue->multidval.val,theatts->att_inq->thevalue->multidval.totalsize);
					return(out_dat);
				}
			}
			theatts = theatts->next;
		}
	}
	return(NULL);
}

static void *GribReadAtt
#if	NhlNeedProto
(void *therec,NclQuark theatt,void* storage)
#else
(therec,theatt,storage)
void * therec;
NclQuark theatt;
void* storage;
#endif
{
return(NULL);
}


static NhlErrorTypes GribSetOption
#if	NhlNeedProto
(void *therec,NclQuark option, NclBasicDataTypes data_type, int n_items, void * values)
#else
(therec,theatt,data_type,n_items,values)
	void *therec;
	NclQuark theatt;
	NclBasicDataTypes data_type;
	int n_items;
	void * values;
#endif
{
	GribFileRecord *rec = (GribFileRecord*)therec;
	int i;

	if (option ==  NrmStringToQuark("thinnedgridinterpolation")) {
		rec->options[GRIB_THINNED_GRID_INTERPOLATION_OPT].values = (void*) *(NrmQuark *)values;
	}

	if (option ==  NrmStringToQuark("initialtimecoordinatetype")) {
		rec->options[GRIB_INITIAL_TIME_COORDINATE_TYPE_OPT].values = (void*) *(NrmQuark *)values;
		SetInitialTimeCoordinates(therec);
	}
	
	if (option ==  NrmStringToQuark("defaultncepptable")) {
		rec->options[GRIB_DEFAULT_NCEP_PTABLE_OPT].values = (void*) *(NrmQuark *)values;
	}
	if (option ==  NrmStringToQuark("printrecordinfo")) {
		rec->options[GRIB_PRINT_RECORD_INFO_OPT].values = (void*) *(int *)values;
	}
	if (option ==  NrmStringToQuark("singleelementdimensions")) {
		/* rec->options[GRIB_SINGLE_ELEMENT_DIMENSIONS_OPT].values = (void*) values; don't need to set this, it would need to be copied */
		rec->options[GRIB_SINGLE_ELEMENT_DIMENSIONS_OPT].n_values = n_items;
		rec->single_dims = GRIB_No_Dims;
		for (i = 0; i < n_items; i++) {
			if (((NrmQuark*)values)[i] == NrmStringToQuark("none")) {
				rec->single_dims = GRIB_No_Dims;
				break;
			}
			else if (((NrmQuark*)values)[i] == NrmStringToQuark("all")) {
				rec->single_dims = GRIB_All_Dims;
				break;
			}
			else if (((NrmQuark*)values)[i] == NrmStringToQuark("ensemble")) {
				rec->single_dims |= GRIB_Ensemble_Dims;
			}
			else if (((NrmQuark*)values)[i] == NrmStringToQuark("probability")) {
				rec->single_dims |= GRIB_Ensemble_Dims;
			}
			else if (((NrmQuark*)values)[i] == NrmStringToQuark("initial_time")) {
				rec->single_dims |= GRIB_Initial_Time_Dims;
			}
			else if (((NrmQuark*)values)[i] == NrmStringToQuark("forecast_time")) {
				rec->single_dims |= GRIB_Forecast_Time_Dims;
			}
			else if (((NrmQuark*)values)[i] == NrmStringToQuark("level")) {
				rec->single_dims |= GRIB_Level_Dims;
			}
		}
	}
	if (option ==  NrmStringToQuark("timeperiodsuffix")) {
		rec->options[GRIB_TIME_PERIOD_SUFFIX_OPT].values = (void*) *(int *)values;
	}
	
	return NhlNOERROR;
}


NclFormatFunctionRec GribRec = {
/* NclInitializeFileRecFunc initialize_file_rec */      GribInitializeFileRec,
/* NclCreateFileFunc	   create_file; */		GribCreateFile,
/* NclOpenFileFunc         open_file; */		GribOpenFile,
/* NclFreeFileRecFunc      free_file_rec; */		GribFreeFileRec,
/* NclGetVarNamesFunc      get_var_names; */		GribGetVarNames,
/* NclGetVarInfoFunc       get_var_info; */		GribGetVarInfo,
/* NclGetDimNamesFunc      get_dim_names; */		GribGetDimNames,
/* NclGetDimInfoFunc       get_dim_info; */		GribGetDimInfo,
/* NclGetAttNamesFunc      get_att_names; */		GribGetAttNames,
/* NclGetAttInfoFunc       get_att_info; */		GribGetAttInfo,
/* NclGetVarAttNamesFunc   get_var_att_names; */	GribGetVarAttNames,
/* NclGetVarAttInfoFunc    get_var_att_info; */		GribGetVarAttInfo,
/* NclGetCoordInfoFunc     get_coord_info; */		GribGetCoordInfo,
/* NclReadCoordFunc        read_coord; */		GribReadCoord,
/* NclReadCoordFunc        read_coord; */		NULL,
/* NclReadVarFunc          read_var; */			GribReadVar,
/* NclReadVarFunc          read_var; */			NULL,
/* NclReadAttFunc          read_att; */			GribReadAtt,
/* NclReadVarAttFunc       read_var_att; */		GribReadVarAtt,
/* NclWriteCoordFunc       write_coord; */		NULL,
/* NclWriteCoordFunc       write_coord; */		NULL,
/* NclWriteVarFunc         write_var; */		NULL,
/* NclWriteVarFunc         write_var; */		NULL,
/* NclWriteAttFunc         write_att; */		NULL,
/* NclWriteVarAttFunc      write_var_att; */		NULL,
/* NclAddDimFunc           add_dim; */			NULL,
/* NclAddChunkDimFunc      add_chunkdim; */		NULL,
/* NclRenameDimFunc        rename_dim; */		NULL,
/* NclAddVarFunc           add_var; */			NULL,
/* NclAddVarChunkFunc      add_var_chunk; */		NULL,
/* NclAddVarChunkCacheFunc add_var_chunk_cache; */	NULL,
/* NclSetVarCompressLevelFunc set_var_compress_level; */ NULL,
/* NclAddVarFunc           add_coord_var; */		NULL,
/* NclAddAttFunc           add_att; */			NULL,
/* NclAddVarAttFunc        add_var_att; */		NULL,
/* NclMapFormatTypeToNcl   map_format_type_to_ncl; */	GribMapToNcl,
/* NclMapNclTypeToFormat   map_ncl_type_to_format; */	GribMapFromNcl,
/* NclDelAttFunc           del_att; */			NULL,
/* NclDelVarAttFunc        del_var_att; */		NULL,
#include "NclGrpFuncs.null"
/* NclSetOptionFunc        set_option;  */              GribSetOption
};
