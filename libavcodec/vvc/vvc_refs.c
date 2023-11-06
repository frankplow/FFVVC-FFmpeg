/*
 * VVC reference management
 *
 * Copyright (C) 2023 Nuo Mi
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdatomic.h>

#include "libavutil/thread.h"
#include "libavcodec/refstruct.h"
#include "libavcodec/thread.h"

#include "vvc_refs.h"

#define VVC_FRAME_FLAG_OUTPUT    (1 << 0)
#define VVC_FRAME_FLAG_SHORT_REF (1 << 1)
#define VVC_FRAME_FLAG_LONG_REF  (1 << 2)
#define VVC_FRAME_FLAG_BUMPING   (1 << 3)

typedef struct FrameProgress {
    atomic_int progress[VVC_PROGRESS_LAST];
    VVCProgressListener *listener[VVC_PROGRESS_LAST];
    AVMutex lock;
    uint8_t has_lock;
} FrameProgress;

void ff_vvc_unref_frame(VVCFrameContext *fc, VVCFrame *frame, int flags)
{
    /* frame->frame can be NULL if context init failed */
    if (!frame->frame || !frame->frame->buf[0])
        return;

    frame->flags &= ~flags;
    if (!frame->flags) {
        av_frame_unref(frame->frame);
        ff_refstruct_unref(&frame->progress);

        ff_refstruct_unref(&frame->tab_dmvr_mvf);

        ff_refstruct_unref(&frame->rpl);
        frame->nb_rpl_elems = 0;
        ff_refstruct_unref(&frame->rpl_tab);

        frame->collocated_ref = NULL;
    }
}

const RefPicList *ff_vvc_get_ref_list(const VVCFrameContext *fc, const VVCFrame *ref, int x0, int y0)
{
    int x_cb         = x0 >> fc->ps.sps->ctb_log2_size_y;
    int y_cb         = y0 >> fc->ps.sps->ctb_log2_size_y;
    int pic_width_cb = fc->ps.pps->ctb_width;
    int ctb_addr_rs  = y_cb * pic_width_cb + x_cb;

    return (const RefPicList *)ref->rpl_tab[ctb_addr_rs];
}

void ff_vvc_clear_refs(VVCFrameContext *fc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++)
        ff_vvc_unref_frame(fc, &fc->DPB[i],
            VVC_FRAME_FLAG_SHORT_REF | VVC_FRAME_FLAG_LONG_REF);
}

static void free_progress(FFRefStructOpaque unused, void *obj)
{
    FrameProgress *p = (FrameProgress *)obj;

    if (p->has_lock)
        ff_mutex_destroy(&p->lock);
}

static FrameProgress *alloc_progress(void)
{
    FrameProgress *p = ff_refstruct_alloc_ext(sizeof(*p), 0, NULL, free_progress);

    if (p) {
        p->has_lock = !ff_mutex_init(&p->lock, NULL);
        if (!p->has_lock)
            ff_refstruct_unref(&p);
    }
    return p;
}

static VVCFrame *alloc_frame(VVCContext *s, VVCFrameContext *fc)
{
    const VVCPPS *pps = fc->ps.pps;
    int i, j, ret;
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];
        if (frame->frame->buf[0])
            continue;

        ret = ff_thread_get_buffer(fc->avctx, frame->frame,
                                   AV_GET_BUFFER_FLAG_REF);
        if (ret < 0)
            return NULL;

        frame->rpl = ff_refstruct_allocz(s->current_frame.nb_units * sizeof(RefPicListTab));
        if (!frame->rpl)
            goto fail;
        frame->nb_rpl_elems = s->current_frame.nb_units;

        frame->tab_dmvr_mvf = ff_refstruct_pool_get(fc->tab_dmvr_mvf_pool);
        if (!frame->tab_dmvr_mvf)
            goto fail;

        frame->rpl_tab = ff_refstruct_pool_get(fc->rpl_tab_pool);
        if (!frame->rpl_tab)
            goto fail;
        frame->ctb_count = pps->ctb_width * pps->ctb_height;
        for (j = 0; j < frame->ctb_count; j++)
            frame->rpl_tab[j] = frame->rpl;

        frame->progress = alloc_progress();
        if (!frame->progress)
            goto fail;

        return frame;
fail:
        ff_vvc_unref_frame(fc, frame, ~0);
        return NULL;
    }
    av_log(s->avctx, AV_LOG_ERROR, "Error allocating frame, DPB full.\n");
    return NULL;
}

int ff_vvc_set_new_ref(VVCContext *s, VVCFrameContext *fc, AVFrame **frame)
{
    const VVCPH *ph= &fc->ps.ph;
    const int poc = ph->poc;
    VVCFrame *ref;
    int i;

    /* check that this POC doesn't already exist */
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];

        if (frame->frame->buf[0] && frame->sequence == s->seq_decode &&
            frame->poc == poc) {
            av_log(s->avctx, AV_LOG_ERROR, "Duplicate POC in a sequence: %d.\n",
                   poc);
            return AVERROR_INVALIDDATA;
        }
    }

    ref = alloc_frame(s, fc);
    if (!ref)
        return AVERROR(ENOMEM);

    *frame = ref->frame;
    fc->ref = ref;

    if (s->no_output_before_recovery_flag && (IS_RASL(s) || !GDR_IS_RECOVERED(s)))
        ref->flags = 0;
    else if (ph->r->ph_pic_output_flag)
        ref->flags = VVC_FRAME_FLAG_OUTPUT;

    if (!ph->r->ph_non_ref_pic_flag)
        ref->flags |= VVC_FRAME_FLAG_SHORT_REF;

    ref->poc      = poc;
    ref->sequence = s->seq_decode;
    ref->frame->crop_left   = fc->ps.pps->r->pps_conf_win_left_offset;
    ref->frame->crop_right  = fc->ps.pps->r->pps_conf_win_right_offset;
    ref->frame->crop_top    = fc->ps.pps->r->pps_conf_win_top_offset;
    ref->frame->crop_bottom = fc->ps.pps->r->pps_conf_win_bottom_offset;

    return 0;
}

int ff_vvc_output_frame(VVCContext *s, VVCFrameContext *fc, AVFrame *out, const int no_output_of_prior_pics_flag, int flush)
{
    const VVCSPS *sps = fc->ps.sps;
    do {
        int nb_output = 0;
        int min_poc   = INT_MAX;
        int i, min_idx, ret;

        if (no_output_of_prior_pics_flag) {
            for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
                VVCFrame *frame = &fc->DPB[i];
                if (!(frame->flags & VVC_FRAME_FLAG_BUMPING) && frame->poc != fc->ps.ph.poc &&
                        frame->sequence == s->seq_output) {
                    ff_vvc_unref_frame(fc, frame, VVC_FRAME_FLAG_OUTPUT);
                }
            }
        }

        for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            VVCFrame *frame = &fc->DPB[i];
            if ((frame->flags & VVC_FRAME_FLAG_OUTPUT) &&
                frame->sequence == s->seq_output) {
                nb_output++;
                if (frame->poc < min_poc || nb_output == 1) {
                    min_poc = frame->poc;
                    min_idx = i;
                }
            }
        }

        /* wait for more frames before output */
        if (!flush && s->seq_output == s->seq_decode && sps &&
            nb_output <= sps->r->sps_dpb_params.dpb_max_dec_pic_buffering_minus1[sps->r->sps_max_sublayers_minus1] + 1)
            return 0;

        if (nb_output) {
            VVCFrame *frame = &fc->DPB[min_idx];

            ret = av_frame_ref(out, frame->frame);
            if (frame->flags & VVC_FRAME_FLAG_BUMPING)
                ff_vvc_unref_frame(fc, frame, VVC_FRAME_FLAG_OUTPUT | VVC_FRAME_FLAG_BUMPING);
            else
                ff_vvc_unref_frame(fc, frame, VVC_FRAME_FLAG_OUTPUT);
            if (ret < 0)
                return ret;

            av_log(s->avctx, AV_LOG_DEBUG,
                   "Output frame with POC %d.\n", frame->poc);
            return 1;
        }

        if (s->seq_output != s->seq_decode)
            s->seq_output = (s->seq_output + 1) & 0xff;
        else
            break;
    } while (1);
    return 0;
}

void ff_vvc_bump_frame(VVCContext *s, VVCFrameContext *fc)
{
    const VVCSPS *sps = fc->ps.sps;
    const int poc = fc->ps.ph.poc;
    int dpb = 0;
    int min_poc = INT_MAX;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];
        if ((frame->flags) &&
            frame->sequence == s->seq_output &&
            frame->poc != poc) {
            dpb++;
        }
    }

    if (sps && dpb >= sps->r->sps_dpb_params.dpb_max_dec_pic_buffering_minus1[sps->r->sps_max_sublayers_minus1] + 1) {
        for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            VVCFrame *frame = &fc->DPB[i];
            if ((frame->flags) &&
                frame->sequence == s->seq_output &&
                frame->poc != poc) {
                if (frame->flags == VVC_FRAME_FLAG_OUTPUT && frame->poc < min_poc) {
                    min_poc = frame->poc;
                }
            }
        }

        for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            VVCFrame *frame = &fc->DPB[i];
            if (frame->flags & VVC_FRAME_FLAG_OUTPUT &&
                frame->sequence == s->seq_output &&
                frame->poc <= min_poc) {
                frame->flags |= VVC_FRAME_FLAG_BUMPING;
            }
        }

        dpb--;
    }
}

static VVCFrame *find_ref_idx(VVCContext *s, VVCFrameContext *fc, int poc, uint8_t use_msb)
{
    int mask = use_msb ? ~0 : fc->ps.sps->max_pic_order_cnt_lsb - 1;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *ref = &fc->DPB[i];
        if (ref->frame->buf[0] && ref->sequence == s->seq_decode) {
            if ((ref->poc & mask) == poc)
                return ref;
        }
    }
    return NULL;
}

static void mark_ref(VVCFrame *frame, int flag)
{
    frame->flags &= ~(VVC_FRAME_FLAG_LONG_REF | VVC_FRAME_FLAG_SHORT_REF);
    frame->flags |= flag;
}

static VVCFrame *generate_missing_ref(VVCContext *s, VVCFrameContext *fc, int poc)
{
    const VVCSPS *sps = fc->ps.sps;
    VVCFrame *frame;
    int i, y;

    frame = alloc_frame(s, fc);
    if (!frame)
        return NULL;

    if (!s->avctx->hwaccel) {
        if (!sps->pixel_shift) {
            for (i = 0; frame->frame->buf[i]; i++)
                memset(frame->frame->buf[i]->data, 1 << (sps->bit_depth - 1),
                       frame->frame->buf[i]->size);
        } else {
            for (i = 0; frame->frame->data[i]; i++)
                for (y = 0; y < (sps->height >> sps->vshift[i]); y++) {
                    uint8_t *dst = frame->frame->data[i] + y * frame->frame->linesize[i];
                    AV_WN16(dst, 1 << (sps->bit_depth - 1));
                    av_memcpy_backptr(dst + 2, 2, 2*(sps->width >> sps->hshift[i]) - 2);
                }
        }
    }

    frame->poc      = poc;
    frame->sequence = s->seq_decode;
    frame->flags    = 0;

    ff_vvc_report_frame_finished(frame);

    return frame;
}

/* add a reference with the given poc to the list and mark it as used in DPB */
static int add_candidate_ref(VVCContext *s, VVCFrameContext *fc, RefPicList *list,
                             int poc, int ref_flag, uint8_t use_msb)
{
    VVCFrame *ref = find_ref_idx(s, fc, poc, use_msb);

    if (ref == fc->ref || list->nb_refs >= VVC_MAX_REF_ENTRIES)
        return AVERROR_INVALIDDATA;

    if (!ref) {
        ref = generate_missing_ref(s, fc, poc);
        if (!ref)
            return AVERROR(ENOMEM);
    }

    list->list[list->nb_refs] = poc;
    list->ref[list->nb_refs]  = ref;
    list->isLongTerm[list->nb_refs] = ref_flag & VVC_FRAME_FLAG_LONG_REF;
    list->nb_refs++;

    mark_ref(ref, ref_flag);
    return 0;
}

static int init_slice_rpl(const VVCFrameContext *fc, SliceContext *sc)
{
    VVCFrame *frame = fc->ref;
    const VVCSH *sh = &sc->sh;

    if (sc->slice_idx >= frame->nb_rpl_elems)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < sh->num_ctus_in_curr_slice; i++) {
        const int rs = sh->ctb_addr_in_curr_slice[i];
        frame->rpl_tab[rs] = frame->rpl + sc->slice_idx;
    }

    sc->rpl = frame->rpl_tab[sh->ctb_addr_in_curr_slice[0]]->refPicList;

    return 0;
}

static int delta_poc_st(const H266RefPicListStruct *rpls,
    const int lx, const int i, const VVCSPS *sps)
{
    int abs_delta_poc_st = rpls->abs_delta_poc_st[i];
    if (!((sps->r->sps_weighted_pred_flag ||
        sps->r->sps_weighted_bipred_flag) && i != 0))
        abs_delta_poc_st++;
    return (1 - 2 * rpls->strp_entry_sign_flag[i]) * abs_delta_poc_st;
}

static int poc_lt(int *prev_delta_poc_msb, const int poc, const H266RefPicLists *ref_lists,
    const int lx, const int j, const int max_poc_lsb)
{
    const H266RefPicListStruct *rpls = ref_lists->rpl_ref_list + lx;
    int lt_poc =  rpls->ltrp_in_header_flag ? ref_lists->poc_lsb_lt[lx][j] : rpls->rpls_poc_lsb_lt[j];

    if (ref_lists->delta_poc_msb_cycle_present_flag[lx][j]) {
        const uint32_t delta = ref_lists->delta_poc_msb_cycle_lt[lx][j] + *prev_delta_poc_msb;
        lt_poc += poc - delta * max_poc_lsb - (poc & (max_poc_lsb - 1));
        *prev_delta_poc_msb = delta;
    }
    return lt_poc;
}

int ff_vvc_slice_rpl(VVCContext *s, VVCFrameContext *fc, SliceContext *sc)
{
    const VVCSPS *sps                = fc->ps.sps;
    const H266RawPPS *pps            = fc->ps.pps->r;
    const VVCPH *ph                  = &fc->ps.ph;
    const H266RawSliceHeader *rsh    = sc->sh.r;
    const int max_poc_lsb            = sps->max_pic_order_cnt_lsb;
    const H266RefPicLists *ref_lists =
        pps->pps_rpl_info_in_ph_flag ?  &ph->r->ph_ref_pic_lists : &rsh->sh_ref_pic_lists;
    int ret = 0;

    ret = init_slice_rpl(fc, sc);
    if (ret < 0)
        return ret;

    for (int lx = L0; lx <= L1; lx++) {
        const H266RefPicListStruct *rpls = ref_lists->rpl_ref_list + lx;
        RefPicList *rpl = sc->rpl + lx;
        int poc_base = ph->poc;
        int prev_delta_poc_msb = 0;

        rpl->nb_refs = 0;
        for (int i = 0, j = 0; i < rpls->num_ref_entries; i++) {
            int poc;
            if (!rpls->inter_layer_ref_pic_flag[i]) {
                int use_msb = 1;
                int ref_flag;
                if (rpls->st_ref_pic_flag[i]) {
                    poc = poc_base + delta_poc_st(rpls, lx, i, sps);
                    poc_base = poc;
                    ref_flag = VVC_FRAME_FLAG_SHORT_REF;
                } else {
                    use_msb = ref_lists->delta_poc_msb_cycle_present_flag[lx][j];
                    poc = poc_lt(&prev_delta_poc_msb, ph->poc, ref_lists, lx, j, max_poc_lsb);
                    ref_flag = VVC_FRAME_FLAG_LONG_REF;
                    j++;
                }
                ret = add_candidate_ref(s, fc, rpl, poc, ref_flag, use_msb);
                if (ret < 0)
                    goto fail;
            } else {
                avpriv_request_sample(fc->avctx, "Inter layer ref");
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }
        }
        if ((!rsh->sh_collocated_from_l0_flag) == lx &&
            rsh->sh_collocated_ref_idx < rpl->nb_refs)
            fc->ref->collocated_ref = rpl->ref[rsh->sh_collocated_ref_idx];
    }
fail:
    return ret;
}

int ff_vvc_frame_rpl(VVCContext *s, VVCFrameContext *fc, SliceContext *sc)
{
    int i, ret = 0;

    /* clear the reference flags on all frames except the current one */
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];

        if (frame == fc->ref)
            continue;

        mark_ref(frame, 0);
    }

    if ((ret = ff_vvc_slice_rpl(s, fc, sc)) < 0)
        goto fail;

fail:
    /* release any frames that are now unused */
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++)
        ff_vvc_unref_frame(fc, &fc->DPB[i], 0);
    return ret;
}

void ff_vvc_report_frame_finished(VVCFrame *frame)
{
    ff_vvc_report_progress(frame, VVC_PROGRESS_MV, INT_MAX);
    ff_vvc_report_progress(frame, VVC_PROGRESS_PIXEL, INT_MAX);
}

static int is_progress_done(const FrameProgress *p, const VVCProgressListener *l)
{
    return p->progress[l->vp] > l->y;
}

static void add_listener(VVCProgressListener **prev, VVCProgressListener *l)
{
    l->next = *prev;
    *prev   = l;
}

static VVCProgressListener* remove_listener(VVCProgressListener **prev, VVCProgressListener *l)
{
    *prev  = l->next;
    l->next = NULL;
    return l;
}

static VVCProgressListener* get_done_listener(FrameProgress *p, const VVCProgress vp)
{
    VVCProgressListener *list = NULL;
    VVCProgressListener **prev = &p->listener[vp];

    while (*prev) {
        if (is_progress_done(p, *prev)) {
            VVCProgressListener *l = remove_listener(prev, *prev);
            add_listener(&list, l);
        } else {
            prev = &(*prev)->next;
        }
    }
    return list;
}

void ff_vvc_report_progress(VVCFrame *frame, const VVCProgress vp, const int y)
{
    FrameProgress *p = frame->progress;
    VVCProgressListener *l = NULL;

    ff_mutex_lock(&p->lock);

    av_assert0(p->progress[vp] < y || p->progress[vp] == INT_MAX);
    p->progress[vp] = y;
    l = get_done_listener(p, vp);

    ff_mutex_unlock(&p->lock);

    while (l) {
        l->progress_done(l);
        l = l->next;
    }
}

int ff_vvc_check_progress(VVCFrame *frame, const VVCProgress vp, const int y)
{
    int ready ;
    FrameProgress *p = frame->progress;

    ff_mutex_lock(&p->lock);

    ready = p->progress[vp] > y + 1;

    ff_mutex_unlock(&p->lock);
    return ready;
}

void ff_vvc_add_progress_listener(VVCFrame *frame, VVCProgressListener *l)
{
    FrameProgress *p = frame->progress;

    ff_mutex_lock(&p->lock);

    if (is_progress_done(p, l)) {
        ff_mutex_unlock(&p->lock);
        l->progress_done(l);
    } else {
        add_listener(p->listener + l->vp, l);
        ff_mutex_unlock(&p->lock);
    }
}
