/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-videoscale
 * @see_also: videorate, ffmpegcolorspace
 *
 * This element resizes video frames. By default the element will try to
 * negotiate to the same size on the source and sinkpad so that no scaling
 * is needed. It is therefore safe to insert this element in a pipeline to
 * get more robust behaviour without any cost if no scaling is needed.
 *
 * This element supports a wide range of color spaces including various YUV and
 * RGB formats and is therefore generally able to operate anywhere in a
 * pipeline.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! ffmpegcolorspace ! videoscale ! ximagesink
 * ]| Decode an Ogg/Theora and display the video using ximagesink. Since
 * ximagesink cannot perform scaling, the video scaling will be performed by
 * videoscale when you resize the video window.
 * To create the test Ogg/Theora file refer to the documentation of theoraenc.
 * |[
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoscale ! video/x-raw-yuv, width=50 ! xvimagesink
 * ]| Decode an Ogg/Theora and display the video using xvimagesink with a width
 * of 50.
 * </refsect2>
 *
 * Last reviewed on 2006-03-02 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/video/video.h>
#include <liboil/liboil.h>

#include "gstvideoscale.h"
#include "vs_image.h"
#include "vs_4tap.h"


/* debug variable definition */
GST_DEBUG_CATEGORY (video_scale_debug);

#define DEFAULT_PROP_METHOD	GST_VIDEO_SCALE_BILINEAR

enum
{
  PROP_0,
  PROP_METHOD
      /* FILL ME */
};

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767]"

static GstStaticCaps gst_video_scale_format_caps[] = {
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_ARGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRA),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_ABGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xBGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y444")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("v308")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y42B")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YVYU")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("UYVY")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YV12")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y41B")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_16),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_15),
  GST_STATIC_CAPS ("video/x-raw-gray, "
      "bpp = 16, "
      "depth = 16, "
      "endianness = BYTE_ORDER, "
      "width = " GST_VIDEO_SIZE_RANGE ", "
      "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE),
  GST_STATIC_CAPS ("video/x-raw-gray, "
      "bpp = 8, "
      "depth = 8, "
      "width = " GST_VIDEO_SIZE_RANGE ", "
      "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y800"))
};

enum
{
  GST_VIDEO_SCALE_RGBA = 0,
  GST_VIDEO_SCALE_ARGB,
  GST_VIDEO_SCALE_BGRA,
  GST_VIDEO_SCALE_ABGR,
  GST_VIDEO_SCALE_AYUV,
  GST_VIDEO_SCALE_RGBx,
  GST_VIDEO_SCALE_xRGB,
  GST_VIDEO_SCALE_BGRx,
  GST_VIDEO_SCALE_xBGR,
  GST_VIDEO_SCALE_Y444,
  GST_VIDEO_SCALE_v308,
  GST_VIDEO_SCALE_RGB,
  GST_VIDEO_SCALE_BGR,
  GST_VIDEO_SCALE_Y42B,
  GST_VIDEO_SCALE_YUY2,
  GST_VIDEO_SCALE_YVYU,
  GST_VIDEO_SCALE_UYVY,
  GST_VIDEO_SCALE_I420,
  GST_VIDEO_SCALE_YV12,
  GST_VIDEO_SCALE_Y41B,
  GST_VIDEO_SCALE_RGB565,
  GST_VIDEO_SCALE_RGB555,
  GST_VIDEO_SCALE_GRAY16,
  GST_VIDEO_SCALE_GRAY8,
  GST_VIDEO_SCALE_Y
};

#define GST_TYPE_VIDEO_SCALE_METHOD (gst_video_scale_method_get_type())
static GType
gst_video_scale_method_get_type (void)
{
  static GType video_scale_method_type = 0;

  static const GEnumValue video_scale_methods[] = {
    {GST_VIDEO_SCALE_NEAREST, "Nearest Neighbour", "nearest-neighbour"},
    {GST_VIDEO_SCALE_BILINEAR, "Bilinear", "bilinear"},
    {GST_VIDEO_SCALE_4TAP, "4-tap", "4-tap"},
    {0, NULL, NULL},
  };

  if (!video_scale_method_type) {
    video_scale_method_type =
        g_enum_register_static ("GstVideoScaleMethod", video_scale_methods);
  }
  return video_scale_method_type;
}

static GstCaps *
gst_video_scale_get_capslist (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    gint i;

    g_assert (caps == NULL);

    caps = gst_caps_new_empty ();
    for (i = 0; i < G_N_ELEMENTS (gst_video_scale_format_caps); i++)
      gst_caps_append (caps,
          gst_caps_make_writable
          (gst_static_caps_get (&gst_video_scale_format_caps[i])));
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstPadTemplate *
gst_video_scale_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_ref (gst_video_scale_get_capslist ()));
}

static GstPadTemplate *
gst_video_scale_sink_template_factory (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_ref (gst_video_scale_get_capslist ()));
}


static void gst_video_scale_finalize (GstVideoScale * videoscale);
static gboolean gst_video_scale_src_event (GstBaseTransform * trans,
    GstEvent * event);

/* base transform vmethods */
static GstCaps *gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_video_scale_set_caps (GstBaseTransform * trans,
    GstCaps * in, GstCaps * out);
static gboolean gst_video_scale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_video_scale_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);
static void gst_video_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static void gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstVideoScale, gst_video_scale, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void
gst_video_scale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Video scaler", "Filter/Effect/Video",
      "Resizes video", "Wim Taymans <wim.taymans@chello.be>");

  gst_element_class_add_pad_template (element_class,
      gst_video_scale_sink_template_factory ());
  gst_element_class_add_pad_template (element_class,
      gst_video_scale_src_template_factory ());
}

static void
gst_video_scale_class_init (GstVideoScaleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = (GObjectFinalizeFunc) gst_video_scale_finalize;
  gobject_class->set_property = gst_video_scale_set_property;
  gobject_class->get_property = gst_video_scale_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_VIDEO_SCALE_METHOD, DEFAULT_PROP_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_scale_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_scale_set_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_video_scale_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_video_scale_transform);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_video_scale_fixate_caps);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_video_scale_src_event);

  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_video_scale_init (GstVideoScale * videoscale, GstVideoScaleClass * klass)
{
  videoscale->tmp_buf = NULL;
  videoscale->method = DEFAULT_PROP_METHOD;
}

static void
gst_video_scale_finalize (GstVideoScale * videoscale)
{
  if (videoscale->tmp_buf)
    g_free (videoscale->tmp_buf);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (videoscale));
}

static void
gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoScale *vscale = GST_VIDEO_SCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      GST_OBJECT_LOCK (vscale);
      vscale->method = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_scale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoScale *vscale = GST_VIDEO_SCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      GST_OBJECT_LOCK (vscale);
      g_value_set_enum (value, vscale->method);
      GST_OBJECT_UNLOCK (vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *ret;
  GstStructure *structure;

  /* this function is always called with a simple caps */
  g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

  ret = gst_caps_copy (caps);
  structure = gst_structure_copy (gst_caps_get_structure (ret, 0));

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  gst_caps_merge_structure (ret, gst_structure_copy (structure));

  /* if pixel aspect ratio, make a range of it */
  if (gst_structure_get_value (structure, "pixel-aspect-ratio")) {
    gst_structure_set (structure,
        "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    gst_caps_merge_structure (ret, structure);
  } else {
    gst_structure_free (structure);
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static int
gst_video_scale_get_format (GstCaps * caps)
{
  gint i;
  GstCaps *icaps, *scaps;

  for (i = 0; i < G_N_ELEMENTS (gst_video_scale_format_caps); i++) {
    scaps = gst_static_caps_get (&gst_video_scale_format_caps[i]);
    icaps = gst_caps_intersect (caps, scaps);
    if (!gst_caps_is_empty (icaps)) {
      gst_caps_unref (icaps);
      return i;
    }
    gst_caps_unref (icaps);
  }

  return -1;
}

/* calculate the size of a buffer */
static gboolean
gst_video_scale_prepare_size (GstVideoScale * videoscale, gint format,
    VSImage * img, gint width, gint height, guint * size)
{
  gboolean res = TRUE;

  img->width = width;
  img->height = height;

  switch (format) {
    case GST_VIDEO_SCALE_RGBx:
    case GST_VIDEO_SCALE_xRGB:
    case GST_VIDEO_SCALE_BGRx:
    case GST_VIDEO_SCALE_xBGR:
    case GST_VIDEO_SCALE_RGBA:
    case GST_VIDEO_SCALE_ARGB:
    case GST_VIDEO_SCALE_BGRA:
    case GST_VIDEO_SCALE_ABGR:
    case GST_VIDEO_SCALE_AYUV:
      img->stride = img->width * 4;
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_Y444:
      img->stride = GST_ROUND_UP_4 (img->width);
      *size = img->stride * img->height * 3;
      break;
    case GST_VIDEO_SCALE_RGB:
    case GST_VIDEO_SCALE_BGR:
    case GST_VIDEO_SCALE_v308:
      img->stride = GST_ROUND_UP_4 (img->width * 3);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_Y42B:
      img->stride = GST_ROUND_UP_4 (img->width);
      *size =
          (GST_ROUND_UP_4 (img->width) +
          GST_ROUND_UP_8 (img->width)) * img->height;
      break;
    case GST_VIDEO_SCALE_YUY2:
    case GST_VIDEO_SCALE_YVYU:
    case GST_VIDEO_SCALE_UYVY:
      img->stride = GST_ROUND_UP_4 (img->width * 2);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_Y41B:
      img->stride = GST_ROUND_UP_4 (img->width);
      *size =
          (GST_ROUND_UP_4 (img->width) +
          (GST_ROUND_UP_16 (img->width) / 2)) * img->height;
      break;
    case GST_VIDEO_SCALE_Y:
    case GST_VIDEO_SCALE_GRAY8:
      img->stride = GST_ROUND_UP_4 (img->width);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_GRAY16:
      img->stride = GST_ROUND_UP_4 (img->width * 2);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_I420:
    case GST_VIDEO_SCALE_YV12:
    {
      gulong img_u_stride, img_u_height;

      img->stride = GST_ROUND_UP_4 (img->width);

      img_u_height = GST_ROUND_UP_2 (img->height) / 2;
      img_u_stride = GST_ROUND_UP_4 (img->stride / 2);

      *size = img->stride * GST_ROUND_UP_2 (img->height) +
          2 * img_u_stride * img_u_height;
      break;
    }
    case GST_VIDEO_SCALE_RGB565:
      img->stride = GST_ROUND_UP_4 (img->width * 2);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_RGB555:
      img->stride = GST_ROUND_UP_4 (img->width * 2);
      *size = img->stride * img->height;
      break;
    default:
      goto unknown_format;
  }

  return res;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (videoscale, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported format %d", videoscale->format));
    return FALSE;
  }
}

static gboolean
parse_caps (GstCaps * caps, gint * format, gint * width, gint * height,
    gboolean * interlaced)
{
  gboolean ret;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);

  if (format)
    *format = gst_video_scale_get_format (caps);

  if (interlaced) {
    *interlaced = FALSE;
    gst_structure_get_boolean (structure, "interlaced", interlaced);
  }

  return ret;
}

static gboolean
gst_video_scale_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (trans);
  gboolean ret;

  ret = parse_caps (in, &videoscale->format, &videoscale->from_width,
      &videoscale->from_height, &videoscale->interlaced);
  ret &=
      parse_caps (out, NULL, &videoscale->to_width, &videoscale->to_height,
      NULL);
  if (!ret)
    goto done;

  if (!(ret = gst_video_scale_prepare_size (videoscale, videoscale->format,
              &videoscale->src, videoscale->from_width, videoscale->from_height,
              &videoscale->src_size)))
    /* prepare size has posted an error when it returns FALSE */
    goto done;

  if (!(ret = gst_video_scale_prepare_size (videoscale, videoscale->format,
              &videoscale->dest, videoscale->to_width, videoscale->to_height,
              &videoscale->dest_size)))
    /* prepare size has posted an error when it returns FALSE */
    goto done;

  if (videoscale->tmp_buf)
    g_free (videoscale->tmp_buf);

  videoscale->tmp_buf =
      g_malloc (videoscale->dest.stride * 4 * (videoscale->interlaced ? 2 : 1));

  /* FIXME: par */
  GST_DEBUG_OBJECT (videoscale, "from=%dx%d, size %d -> to=%dx%d, size %d",
      videoscale->from_width, videoscale->from_height, videoscale->src_size,
      videoscale->to_width, videoscale->to_height, videoscale->dest_size);

done:
  return ret;
}

static gboolean
gst_video_scale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (trans);
  gint format, width, height;
  VSImage img;

  g_assert (size);

  if (!parse_caps (caps, &format, &width, &height, NULL))
    return FALSE;

  if (!gst_video_scale_prepare_size (videoscale, format, &img, width, height,
          size))
    return FALSE;

  return TRUE;
}

static void
gst_video_scale_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;

  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* we have both PAR but they might not be fixated */
  if (from_par && to_par) {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;

    gint count = 0, w = 0, h = 0;

    guint num, den;

    /* from_par should be fixed */
    g_return_if_fail (gst_value_is_fixed (from_par));

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    /* fixate the out PAR */
    if (!gst_value_is_fixed (to_par)) {
      GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", from_par_n,
          from_par_d);
      gst_structure_fixate_field_nearest_fraction (outs, "pixel-aspect-ratio",
          from_par_n, from_par_d);
    }

    to_par_n = gst_value_get_fraction_numerator (to_par);
    to_par_d = gst_value_get_fraction_denominator (to_par);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (gst_structure_get_int (outs, "width", &w))
      ++count;
    if (gst_structure_get_int (outs, "height", &h))
      ++count;
    if (count == 2) {
      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      return;
    }

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    if (!gst_video_calculate_display_ratio (&num, &den, from_w, from_h,
            from_par_n, from_par_d, to_par_n, to_par_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      return;
    }

    GST_DEBUG_OBJECT (base,
        "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
        from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
    GST_DEBUG_OBJECT (base, "resulting output should respect ratio of %d/%d",
        num, den);

    /* now find a width x height that respects this display ratio.
     * prefer those that have one of w/h the same as the incoming video
     * using wd / hd = num / den */

    /* if one of the output width or height is fixed, we work from there */
    if (h) {
      GST_DEBUG_OBJECT (base, "height is fixed,scaling width");
      w = (guint) gst_util_uint64_scale_int (h, num, den);
    } else if (w) {
      GST_DEBUG_OBJECT (base, "width is fixed, scaling height");
      h = (guint) gst_util_uint64_scale_int (w, den, num);
    } else {
      /* none of width or height is fixed, figure out both of them based only on
       * the input width and height */
      /* check hd / den is an integer scale factor, and scale wd with the PAR */
      if (from_h % den == 0) {
        GST_DEBUG_OBJECT (base, "keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      } else if (from_w % num == 0) {
        GST_DEBUG_OBJECT (base, "keeping video width");
        w = from_w;
        h = (guint) gst_util_uint64_scale_int (w, den, num);
      } else {
        GST_DEBUG_OBJECT (base, "approximating but keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      }
    }
    GST_DEBUG_OBJECT (base, "scaling to %dx%d", w, h);

    /* now fixate */
    gst_structure_fixate_field_nearest_int (outs, "width", w);
    gst_structure_fixate_field_nearest_int (outs, "height", h);
  } else {
    gint width, height;

    if (gst_structure_get_int (ins, "width", &width)) {
      if (gst_structure_has_field (outs, "width")) {
        gst_structure_fixate_field_nearest_int (outs, "width", width);
      }
    }
    if (gst_structure_get_int (ins, "height", &height)) {
      if (gst_structure_has_field (outs, "height")) {
        gst_structure_fixate_field_nearest_int (outs, "height", height);
      }
    }
  }

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
}

static gboolean
gst_video_scale_prepare_image (gint format, GstBuffer * buf,
    VSImage * img, VSImage * img_u, VSImage * img_v, gint step,
    gboolean interlaced)
{
  gboolean res = TRUE;

  switch (format) {
    case GST_VIDEO_SCALE_I420:
    case GST_VIDEO_SCALE_YV12:
      img_u->pixels =
          GST_BUFFER_DATA (buf) + GST_ROUND_UP_2 (img->height) * img->stride;
      img_u->height = GST_ROUND_UP_2 (img->height) / 2;
      img_u->width = GST_ROUND_UP_2 (img->width) / 2;
      img_u->stride = GST_ROUND_UP_4 (img_u->width);
      memcpy (img_v, img_u, sizeof (*img_v));
      img_v->pixels = img_u->pixels + img_u->height * img_u->stride;
      if (interlaced && step == 1) {
        img_v->pixels += img_v->stride;
        img_u->pixels += img_u->stride;
      }

      if (interlaced) {
        img_u->height = (img_u->height / 2) + ((step == 0
                && img_u->height % 2 == 1) ? 1 : 0);
        img_u->stride *= 2;
        img_v->height = (img_v->height / 2) + ((step == 0
                && img_v->height % 2 == 1) ? 1 : 0);
        img_v->stride *= 2;
      }

      break;
    case GST_VIDEO_SCALE_Y444:
      img_u->pixels =
          GST_BUFFER_DATA (buf) + GST_ROUND_UP_4 (img->width) * img->height;
      img_u->height = img->height;
      img_u->width = img->width;
      img_u->stride = img->stride;
      memcpy (img_v, img_u, sizeof (*img_v));
      img_v->pixels =
          GST_BUFFER_DATA (buf) + GST_ROUND_UP_4 (img->width) * img->height * 2;

      if (interlaced && step == 1) {
        img_v->pixels += img_v->stride;
        img_u->pixels += img_u->stride;
      }

      if (interlaced) {
        img_u->height = (img_u->height / 2) + ((step == 0
                && img_u->height % 2 == 1) ? 1 : 0);
        img_u->stride *= 2;
        img_v->height = (img_v->height / 2) + ((step == 0
                && img_v->height % 2 == 1) ? 1 : 0);
        img_v->stride *= 2;
      }
      break;
    case GST_VIDEO_SCALE_Y42B:
      img_u->pixels =
          GST_BUFFER_DATA (buf) + GST_ROUND_UP_4 (img->width) * img->height;
      img_u->height = img->height;
      img_u->width = GST_ROUND_UP_2 (img->width) / 2;
      img_u->stride = GST_ROUND_UP_8 (img->width) / 2;
      memcpy (img_v, img_u, sizeof (*img_v));
      img_v->pixels =
          GST_BUFFER_DATA (buf) + (GST_ROUND_UP_4 (img->width) +
          (GST_ROUND_UP_8 (img->width) / 2)) * img->height;

      if (interlaced && step == 1) {
        img_v->pixels += img_v->stride;
        img_u->pixels += img_u->stride;
      }

      if (interlaced) {
        img_u->height = (img_u->height / 2) + ((step == 0
                && img_u->height % 2 == 1) ? 1 : 0);
        img_u->stride *= 2;
        img_v->height = (img_v->height / 2) + ((step == 0
                && img_v->height % 2 == 1) ? 1 : 0);
        img_v->stride *= 2;
      }
      break;
    case GST_VIDEO_SCALE_Y41B:
      img_u->pixels =
          GST_BUFFER_DATA (buf) + GST_ROUND_UP_4 (img->width) * img->height;
      img_u->height = img->height;
      img_u->width = GST_ROUND_UP_4 (img->width) / 4;
      img_u->stride = GST_ROUND_UP_16 (img->width) / 4;
      memcpy (img_v, img_u, sizeof (*img_v));
      img_v->pixels =
          GST_BUFFER_DATA (buf) + (GST_ROUND_UP_4 (img->width) +
          (GST_ROUND_UP_16 (img->width) / 4)) * img->height;

      if (interlaced && step == 1) {
        img_v->pixels += img_v->stride;
        img_u->pixels += img_u->stride;
      }

      if (interlaced) {
        img_u->height = (img_u->height / 2) + ((step == 0
                && img_u->height % 2 == 1) ? 1 : 0);
        img_u->stride *= 2;
        img_v->height = (img_v->height / 2) + ((step == 0
                && img_v->height % 2 == 1) ? 1 : 0);
        img_v->stride *= 2;
      }
      break;
    default:
      break;
  }
  return res;
}

static GstFlowReturn
gst_video_scale_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (trans);
  GstFlowReturn ret = GST_FLOW_OK;
  VSImage dest = videoscale->dest;
  VSImage src = videoscale->src;
  VSImage dest_u = { NULL, };
  VSImage dest_v = { NULL, };
  VSImage src_u = { NULL, };
  VSImage src_v = { NULL, };
  gint method;
  gint step;
  gboolean interlaced = videoscale->interlaced;

  GST_OBJECT_LOCK (videoscale);
  method = videoscale->method;
  GST_OBJECT_UNLOCK (videoscale);

  src.pixels = GST_BUFFER_DATA (in);
  dest.pixels = GST_BUFFER_DATA (out);

  /* For interlaced content we have to run two times with half height
   * and doubled stride */
  if (interlaced) {
    dest.height /= 2;
    src.height /= 2;
    dest.stride *= 2;
    src.stride *= 2;
  }

  if (src.height < 4 && method == GST_VIDEO_SCALE_4TAP)
    method = GST_VIDEO_SCALE_BILINEAR;

  for (step = 0; step < (interlaced ? 2 : 1); step++) {
    gst_video_scale_prepare_image (videoscale->format, in, &videoscale->src,
        &src_u, &src_v, step, interlaced);
    gst_video_scale_prepare_image (videoscale->format, out, &videoscale->dest,
        &dest_u, &dest_v, step, interlaced);

    if (step == 0 && interlaced) {
      if (videoscale->from_height % 2 == 1) {
        src.height += 1;
      }

      if (videoscale->to_height % 2 == 1) {
        dest.height += 1;
      }
    } else if (step == 1 && interlaced) {
      if (videoscale->from_height % 2 == 1) {
        src.height -= 1;
      }

      if (videoscale->to_height % 2 == 1) {
        dest.height -= 1;
      }
      src.pixels += (src.stride / 2);
      dest.pixels += (dest.stride / 2);
    }

    switch (method) {
      case GST_VIDEO_SCALE_NEAREST:
        GST_LOG_OBJECT (videoscale, "doing nearest scaling");
        switch (videoscale->format) {
          case GST_VIDEO_SCALE_RGBx:
          case GST_VIDEO_SCALE_xRGB:
          case GST_VIDEO_SCALE_BGRx:
          case GST_VIDEO_SCALE_xBGR:
          case GST_VIDEO_SCALE_RGBA:
          case GST_VIDEO_SCALE_ARGB:
          case GST_VIDEO_SCALE_BGRA:
          case GST_VIDEO_SCALE_ABGR:
          case GST_VIDEO_SCALE_AYUV:
            vs_image_scale_nearest_RGBA (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB:
          case GST_VIDEO_SCALE_BGR:
          case GST_VIDEO_SCALE_v308:
            vs_image_scale_nearest_RGB (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_YUY2:
          case GST_VIDEO_SCALE_YVYU:
            vs_image_scale_nearest_YUYV (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_UYVY:
            vs_image_scale_nearest_UYVY (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_Y:
          case GST_VIDEO_SCALE_GRAY8:
            vs_image_scale_nearest_Y (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_GRAY16:
            vs_image_scale_nearest_Y16 (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_I420:
          case GST_VIDEO_SCALE_YV12:
          case GST_VIDEO_SCALE_Y444:
          case GST_VIDEO_SCALE_Y42B:
          case GST_VIDEO_SCALE_Y41B:
            vs_image_scale_nearest_Y (&dest, &src, videoscale->tmp_buf);
            vs_image_scale_nearest_Y (&dest_u, &src_u, videoscale->tmp_buf);
            vs_image_scale_nearest_Y (&dest_v, &src_v, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB565:
            vs_image_scale_nearest_RGB565 (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB555:
            vs_image_scale_nearest_RGB555 (&dest, &src, videoscale->tmp_buf);
            break;
          default:
            goto unsupported;
        }
        break;
      case GST_VIDEO_SCALE_BILINEAR:
        GST_LOG_OBJECT (videoscale, "doing bilinear scaling");
        switch (videoscale->format) {
          case GST_VIDEO_SCALE_RGBx:
          case GST_VIDEO_SCALE_xRGB:
          case GST_VIDEO_SCALE_BGRx:
          case GST_VIDEO_SCALE_xBGR:
          case GST_VIDEO_SCALE_RGBA:
          case GST_VIDEO_SCALE_ARGB:
          case GST_VIDEO_SCALE_BGRA:
          case GST_VIDEO_SCALE_ABGR:
          case GST_VIDEO_SCALE_AYUV:
            vs_image_scale_linear_RGBA (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB:
          case GST_VIDEO_SCALE_BGR:
          case GST_VIDEO_SCALE_v308:
            vs_image_scale_linear_RGB (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_YUY2:
          case GST_VIDEO_SCALE_YVYU:
            vs_image_scale_linear_YUYV (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_UYVY:
            vs_image_scale_linear_UYVY (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_Y:
          case GST_VIDEO_SCALE_GRAY8:
            vs_image_scale_linear_Y (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_GRAY16:
            vs_image_scale_linear_Y16 (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_I420:
          case GST_VIDEO_SCALE_YV12:
          case GST_VIDEO_SCALE_Y444:
          case GST_VIDEO_SCALE_Y42B:
          case GST_VIDEO_SCALE_Y41B:
            vs_image_scale_linear_Y (&dest, &src, videoscale->tmp_buf);
            vs_image_scale_linear_Y (&dest_u, &src_u, videoscale->tmp_buf);
            vs_image_scale_linear_Y (&dest_v, &src_v, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB565:
            vs_image_scale_linear_RGB565 (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB555:
            vs_image_scale_linear_RGB555 (&dest, &src, videoscale->tmp_buf);
            break;
          default:
            goto unsupported;
        }
        break;
      case GST_VIDEO_SCALE_4TAP:
        GST_LOG_OBJECT (videoscale, "doing 4tap scaling");
        switch (videoscale->format) {
          case GST_VIDEO_SCALE_RGBx:
          case GST_VIDEO_SCALE_xRGB:
          case GST_VIDEO_SCALE_BGRx:
          case GST_VIDEO_SCALE_xBGR:
          case GST_VIDEO_SCALE_RGBA:
          case GST_VIDEO_SCALE_ARGB:
          case GST_VIDEO_SCALE_BGRA:
          case GST_VIDEO_SCALE_ABGR:
          case GST_VIDEO_SCALE_AYUV:
            vs_image_scale_4tap_RGBA (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB:
          case GST_VIDEO_SCALE_BGR:
          case GST_VIDEO_SCALE_v308:
            vs_image_scale_4tap_RGB (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_YUY2:
          case GST_VIDEO_SCALE_YVYU:
            vs_image_scale_4tap_YUYV (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_UYVY:
            vs_image_scale_4tap_UYVY (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_Y:
          case GST_VIDEO_SCALE_GRAY8:
            vs_image_scale_4tap_Y (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_GRAY16:
            vs_image_scale_4tap_Y16 (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_I420:
          case GST_VIDEO_SCALE_YV12:
          case GST_VIDEO_SCALE_Y444:
          case GST_VIDEO_SCALE_Y42B:
          case GST_VIDEO_SCALE_Y41B:
            vs_image_scale_4tap_Y (&dest, &src, videoscale->tmp_buf);
            vs_image_scale_4tap_Y (&dest_u, &src_u, videoscale->tmp_buf);
            vs_image_scale_4tap_Y (&dest_v, &src_v, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB565:
            vs_image_scale_4tap_RGB565 (&dest, &src, videoscale->tmp_buf);
            break;
          case GST_VIDEO_SCALE_RGB555:
            vs_image_scale_4tap_RGB555 (&dest, &src, videoscale->tmp_buf);
            break;
          default:
            goto unsupported;
        }
        break;
      default:
        goto unknown_mode;
    }

  }

  GST_LOG_OBJECT (videoscale, "pushing buffer of %d bytes",
      GST_BUFFER_SIZE (out));

  return ret;

  /* ERRORS */
unsupported:
  {
    GST_ELEMENT_ERROR (videoscale, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported format %d for scaling method %d",
            videoscale->format, method));
    return GST_FLOW_ERROR;
  }
unknown_mode:
  {
    GST_ELEMENT_ERROR (videoscale, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unknown scaling method %d", videoscale->method));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_video_scale_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (trans);
  gboolean ret;
  gdouble a;
  GstStructure *structure;

  GST_DEBUG_OBJECT (videoscale, "handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * videoscale->from_width / videoscale->to_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * videoscale->from_height / videoscale->to_height, NULL);
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  oil_init ();

  if (!gst_element_register (plugin, "videoscale", GST_RANK_NONE,
          GST_TYPE_VIDEO_SCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (video_scale_debug, "videoscale", 0,
      "videoscale element");

  vs_4tap_init ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoscale",
    "Resizes video", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
