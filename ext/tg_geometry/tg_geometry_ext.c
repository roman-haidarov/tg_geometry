#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "ruby.h"
#include "ruby/encoding.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "tg.h"
#include "rtree.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static VALUE mTG;
static VALUE mTGGeometry;
static VALUE cTGGeometryGeom;
static VALUE cTGGeometryRect;
static VALUE cTGGeometryIndex;
static VALUE cTGGeometryLine;
static VALUE cTGGeometryRing;
static VALUE cTGGeometryPolygon;
static VALUE cTGGeometrySegment;
static VALUE eTGGeometryError;
static VALUE eTGGeometryParseError;
static VALUE eTGGeometryArgumentError;
static VALUE eTGGeometryFrozenIndexError;

typedef struct {
    VALUE geom_owner;
    struct tg_geom *geom;
    size_t geom_bytes;
    bool owned;
} tg_geom_wrapper_t;

typedef struct {
    double min_x;
    double min_y;
    double max_x;
    double max_y;
    bool initialized;
} tg_rect_wrapper_t;

typedef struct {
    VALUE geom_owner;
    const struct tg_line *line;
} tg_line_wrapper_t;

typedef struct {
    VALUE geom_owner;
    const struct tg_ring *ring;
} tg_ring_wrapper_t;

typedef struct {
    VALUE geom_owner;
    const struct tg_poly *poly;
} tg_polygon_wrapper_t;

typedef struct {
    struct tg_segment segment;
    bool initialized;
} tg_segment_wrapper_t;

enum tg_geometry_index_via {
    TG_GEOMETRY_INDEX_VIA_GEOM,
    TG_GEOMETRY_INDEX_VIA_GEOJSON,
    TG_GEOMETRY_INDEX_VIA_WKB,
};

enum tg_geometry_index_strategy {
    TG_GEOMETRY_INDEX_STRATEGY_FLAT,
    TG_GEOMETRY_INDEX_STRATEGY_RTREE,
};

enum tg_geometry_index_predicate {
    TG_GEOMETRY_INDEX_PREDICATE_COVERS,
    TG_GEOMETRY_INDEX_PREDICATE_CONTAINS,
};

typedef struct {
    VALUE id;
    VALUE geom_owner;
    struct tg_geom *geom;
    struct tg_rect bbox;
    size_t geom_bytes;
    long ordinal;
    bool owned;
} tg_index_entry_t;

typedef struct {
    tg_index_entry_t *entries;
    long len;
    long initialized;
    long capacity;

    enum tg_geometry_index_strategy strategy;
    enum tg_geometry_index_predicate predicate;
    struct rtree *rtree;

    bool frozen;

    size_t owned_geom_bytes_total;
    size_t rtree_bytes;
    size_t entries_bytes;
    struct tg_rect bbox;
    bool has_bbox;
} tg_index_t;

typedef struct {
    tg_index_t *owner;
    size_t size;
} tg_rtree_alloc_header_t;

static _Thread_local tg_index_t *tg_current_rtree_owner = NULL;

static ID id_format;
static ID id_index;
static ID id_auto;
static ID id_geojson;
static ID id_wkt;
static ID id_wkb;
static ID id_hex;
static ID id_geobin;
static ID id_default;
static ID id_none;
static ID id_natural;
static ID id_ystripes;
static ID id_via;
static ID id_strategy;
static ID id_predicate;
static ID id_geometry_index;
static ID id_geom;
static ID id_flat;
static ID id_rtree;
static ID id_covers;
static ID id_contains;

#ifdef TG_DEBUG_TEST
static bool tg_debug_fail_next_entries_alloc = false;
static long tg_debug_fail_rtree_alloc_countdown = -1;
static bool tg_debug_fail_next_match_buffer_alloc = false;
#endif

static void tg_geometry_vendor_header_sanity(void) {
    struct tg_point p = {0.0, 0.0};
    struct tg_rect rect = {p, p};

    (void)rect;
    (void)TG_DEFAULT;
    (void)rtree_new_with_allocator;
}

static void geom_mark(void *ptr) {
    tg_geom_wrapper_t *w = (tg_geom_wrapper_t *)ptr;

    if (w && !NIL_P(w->geom_owner)) {
        rb_gc_mark_movable(w->geom_owner);
    }
}

static void geom_compact(void *ptr) {
    tg_geom_wrapper_t *w = (tg_geom_wrapper_t *)ptr;

    if (w && !NIL_P(w->geom_owner)) {
        w->geom_owner = rb_gc_location(w->geom_owner);
    }
}

static void geom_free(void *ptr) {
    tg_geom_wrapper_t *w = (tg_geom_wrapper_t *)ptr;
    if (!w)
        return;

    if (w->owned && w->geom) {
        if (w->geom_bytes > 0) {
            rb_gc_adjust_memory_usage(-(ssize_t)w->geom_bytes);
        }
        tg_geom_free(w->geom);
    }

    w->geom = NULL;
    w->geom_owner = Qnil;
    w->geom_bytes = 0;
    w->owned = false;

    ruby_xfree(w);
}

static size_t geom_memsize(const void *ptr) {
    const tg_geom_wrapper_t *w = (const tg_geom_wrapper_t *)ptr;
    if (!w)
        return 0;

    return sizeof(*w) + ((w->owned && w->geom) ? w->geom_bytes : 0);
}

static const rb_data_type_t tg_geom_type = {
    "TG::Geometry::Geom",
    {
        geom_mark,
        geom_free,
        geom_memsize,
        geom_compact,
        {0},
    },
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static void rect_free(void *ptr) {
    ruby_xfree(ptr);
}

static size_t rect_memsize(const void *ptr) {
    (void)ptr;
    return sizeof(tg_rect_wrapper_t);
}

static const rb_data_type_t tg_rect_type = {
    "TG::Geometry::Rect",
    {
        NULL,
        rect_free,
        rect_memsize,
        NULL,
        {0},
    },
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static void borrowed_line_mark(void *ptr) {
    tg_line_wrapper_t *w = (tg_line_wrapper_t *)ptr;
    if (w && !NIL_P(w->geom_owner)) {
        rb_gc_mark_movable(w->geom_owner);
    }
}

static void borrowed_line_compact(void *ptr) {
    tg_line_wrapper_t *w = (tg_line_wrapper_t *)ptr;
    if (w && !NIL_P(w->geom_owner)) {
        w->geom_owner = rb_gc_location(w->geom_owner);
    }
}

static void borrowed_line_free(void *ptr) {
    ruby_xfree(ptr);
}

static size_t borrowed_line_memsize(const void *ptr) {
    (void)ptr;
    return sizeof(tg_line_wrapper_t);
}

static const rb_data_type_t tg_line_type = {
    "TG::Geometry::Line",
    {
        borrowed_line_mark,
        borrowed_line_free,
        borrowed_line_memsize,
        borrowed_line_compact,
        {0},
    },
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static void borrowed_ring_mark(void *ptr) {
    tg_ring_wrapper_t *w = (tg_ring_wrapper_t *)ptr;
    if (w && !NIL_P(w->geom_owner)) {
        rb_gc_mark_movable(w->geom_owner);
    }
}

static void borrowed_ring_compact(void *ptr) {
    tg_ring_wrapper_t *w = (tg_ring_wrapper_t *)ptr;
    if (w && !NIL_P(w->geom_owner)) {
        w->geom_owner = rb_gc_location(w->geom_owner);
    }
}

static void borrowed_ring_free(void *ptr) {
    ruby_xfree(ptr);
}

static size_t borrowed_ring_memsize(const void *ptr) {
    (void)ptr;
    return sizeof(tg_ring_wrapper_t);
}

static const rb_data_type_t tg_ring_type = {
    "TG::Geometry::Ring",
    {
        borrowed_ring_mark,
        borrowed_ring_free,
        borrowed_ring_memsize,
        borrowed_ring_compact,
        {0},
    },
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static void borrowed_polygon_mark(void *ptr) {
    tg_polygon_wrapper_t *w = (tg_polygon_wrapper_t *)ptr;
    if (w && !NIL_P(w->geom_owner)) {
        rb_gc_mark_movable(w->geom_owner);
    }
}

static void borrowed_polygon_compact(void *ptr) {
    tg_polygon_wrapper_t *w = (tg_polygon_wrapper_t *)ptr;
    if (w && !NIL_P(w->geom_owner)) {
        w->geom_owner = rb_gc_location(w->geom_owner);
    }
}

static void borrowed_polygon_free(void *ptr) {
    ruby_xfree(ptr);
}

static size_t borrowed_polygon_memsize(const void *ptr) {
    (void)ptr;
    return sizeof(tg_polygon_wrapper_t);
}

static const rb_data_type_t tg_polygon_type = {
    "TG::Geometry::Polygon",
    {
        borrowed_polygon_mark,
        borrowed_polygon_free,
        borrowed_polygon_memsize,
        borrowed_polygon_compact,
        {0},
    },
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static void segment_free(void *ptr) {
    ruby_xfree(ptr);
}

static size_t segment_memsize(const void *ptr) {
    (void)ptr;
    return sizeof(tg_segment_wrapper_t);
}

static const rb_data_type_t tg_segment_type = {
    "TG::Geometry::Segment",
    {
        NULL,
        segment_free,
        segment_memsize,
        NULL,
        {0},
    },
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static void index_dispose(tg_index_t *idx) {
    if (!idx)
        return;

    if (idx->rtree) {
        rtree_free(idx->rtree);
        idx->rtree = NULL;
    }

    if (idx->entries) {
        for (long i = 0; i < idx->initialized; i++) {
            tg_index_entry_t *entry = &idx->entries[i];

            if (entry->owned && entry->geom) {
                if (entry->geom_bytes > 0) {
                    rb_gc_adjust_memory_usage(-(ssize_t)entry->geom_bytes);
                    if (idx->owned_geom_bytes_total >= entry->geom_bytes) {
                        idx->owned_geom_bytes_total -= entry->geom_bytes;
                    } else {
                        idx->owned_geom_bytes_total = 0;
                    }
                    entry->geom_bytes = 0;
                }

                tg_geom_free(entry->geom);
                entry->geom = NULL;
            }

            entry->id = Qnil;
            entry->geom_owner = Qnil;
            entry->owned = false;
        }

        free(idx->entries);
        idx->entries = NULL;

        if (idx->entries_bytes > 0) {
            rb_gc_adjust_memory_usage(-(ssize_t)idx->entries_bytes);
        }
    }

    idx->len = 0;
    idx->initialized = 0;
    idx->capacity = 0;
    idx->frozen = false;
    idx->owned_geom_bytes_total = 0;
    idx->rtree_bytes = 0;
    idx->entries_bytes = 0;
    idx->has_bbox = false;
}

static void index_mark(void *ptr) {
    tg_index_t *idx = (tg_index_t *)ptr;

    if (!idx || !idx->entries)
        return;

    for (long i = 0; i < idx->initialized; i++) {
        rb_gc_mark_movable(idx->entries[i].id);

        if (!NIL_P(idx->entries[i].geom_owner)) {
            rb_gc_mark_movable(idx->entries[i].geom_owner);
        }
    }
}

static void index_compact(void *ptr) {
    tg_index_t *idx = (tg_index_t *)ptr;

    if (!idx || !idx->entries)
        return;

    for (long i = 0; i < idx->initialized; i++) {
        idx->entries[i].id = rb_gc_location(idx->entries[i].id);

        if (!NIL_P(idx->entries[i].geom_owner)) {
            idx->entries[i].geom_owner = rb_gc_location(idx->entries[i].geom_owner);
        }
    }
}

static void index_free(void *ptr) {
    tg_index_t *idx = (tg_index_t *)ptr;

    index_dispose(idx);
    ruby_xfree(idx);
}

static size_t index_memsize(const void *ptr) {
    const tg_index_t *idx = (const tg_index_t *)ptr;

    if (!idx)
        return 0;

    return sizeof(*idx) + idx->entries_bytes + idx->owned_geom_bytes_total + idx->rtree_bytes;
}

static const rb_data_type_t tg_index_type = {
    "TG::Geometry::Index",
    {
        index_mark,
        index_free,
        index_memsize,
        index_compact,
        {0},
    },
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE kwargs_value(VALUE kwargs, ID key, VALUE fallback) {
    VALUE value;

    if (NIL_P(kwargs))
        return fallback;

    value = rb_hash_aref(kwargs, ID2SYM(key));
    return NIL_P(value) ? fallback : value;
}

static enum tg_index parse_index_symbol(VALUE value) {
    ID id;

    if (!SYMBOL_P(value)) {
        rb_raise(eTGGeometryArgumentError,
                 "index: must be one of :default, :none, :natural, :ystripes");
    }

    id = SYM2ID(value);
    if (id == id_default)
        return TG_DEFAULT;
    if (id == id_none)
        return TG_NONE;
    if (id == id_natural)
        return TG_NATURAL;
    if (id == id_ystripes)
        return TG_YSTRIPES;

    rb_raise(eTGGeometryArgumentError,
             "index: must be one of :default, :none, :natural, :ystripes");
}

enum tg_geometry_parse_format {
    TG_GEOMETRY_FORMAT_AUTO,
    TG_GEOMETRY_FORMAT_GEOJSON,
    TG_GEOMETRY_FORMAT_WKT,
    TG_GEOMETRY_FORMAT_WKB,
    TG_GEOMETRY_FORMAT_HEX,
    TG_GEOMETRY_FORMAT_GEOBIN,
};

static enum tg_geometry_parse_format parse_format_symbol(VALUE value) {
    ID id;

    if (!SYMBOL_P(value)) {
        rb_raise(eTGGeometryArgumentError,
                 "format: must be one of :auto, :geojson, :wkt, :wkb, :hex, :geobin");
    }

    id = SYM2ID(value);
    if (id == id_auto)
        return TG_GEOMETRY_FORMAT_AUTO;
    if (id == id_geojson)
        return TG_GEOMETRY_FORMAT_GEOJSON;
    if (id == id_wkt)
        return TG_GEOMETRY_FORMAT_WKT;
    if (id == id_wkb)
        return TG_GEOMETRY_FORMAT_WKB;
    if (id == id_hex)
        return TG_GEOMETRY_FORMAT_HEX;
    if (id == id_geobin)
        return TG_GEOMETRY_FORMAT_GEOBIN;

    rb_raise(eTGGeometryArgumentError,
             "format: must be one of :auto, :geojson, :wkt, :wkb, :hex, :geobin");
}

static VALUE required_kwargs_value(VALUE kwargs, ID key, const char *name) {
    VALUE value;

    if (NIL_P(kwargs)) {
        rb_raise(eTGGeometryArgumentError, "%s is required", name);
    }

    value = rb_hash_aref(kwargs, ID2SYM(key));
    if (NIL_P(value)) {
        rb_raise(eTGGeometryArgumentError, "%s is required", name);
    }

    return value;
}

static enum tg_geometry_index_via parse_index_via_symbol(VALUE value) {
    ID id;

    if (!SYMBOL_P(value)) {
        rb_raise(eTGGeometryArgumentError, "via: must be one of :geom, :geojson, :wkb");
    }

    id = SYM2ID(value);
    if (id == id_geom)
        return TG_GEOMETRY_INDEX_VIA_GEOM;
    if (id == id_geojson)
        return TG_GEOMETRY_INDEX_VIA_GEOJSON;
    if (id == id_wkb)
        return TG_GEOMETRY_INDEX_VIA_WKB;

    rb_raise(eTGGeometryArgumentError, "via: must be one of :geom, :geojson, :wkb");
}

static enum tg_geometry_index_strategy parse_index_strategy_symbol(VALUE value) {
    ID id;

    if (!SYMBOL_P(value)) {
        rb_raise(eTGGeometryArgumentError, "strategy: must be one of :flat, :rtree");
    }

    id = SYM2ID(value);
    if (id == id_flat)
        return TG_GEOMETRY_INDEX_STRATEGY_FLAT;
    if (id == id_rtree)
        return TG_GEOMETRY_INDEX_STRATEGY_RTREE;
    rb_raise(eTGGeometryArgumentError, "strategy: must be one of :flat, :rtree");
}

static enum tg_geometry_index_predicate parse_index_predicate_symbol(VALUE value) {
    ID id;

    if (!SYMBOL_P(value)) {
        rb_raise(eTGGeometryArgumentError, "predicate: must be one of :covers, :contains");
    }

    id = SYM2ID(value);
    if (id == id_covers)
        return TG_GEOMETRY_INDEX_PREDICATE_COVERS;
    if (id == id_contains)
        return TG_GEOMETRY_INDEX_PREDICATE_CONTAINS;

    rb_raise(eTGGeometryArgumentError, "predicate: must be one of :covers, :contains");
}

static VALUE geom_wrap_owned(struct tg_geom *geom) {
    tg_geom_wrapper_t *w;
    VALUE wrapper;

    if (!geom) {
        rb_raise(rb_eNoMemError, "TG geometry allocation failed");
    }

    wrapper = TypedData_Make_Struct(cTGGeometryGeom, tg_geom_wrapper_t, &tg_geom_type, w);

    w->geom_owner = Qnil;
    w->geom = geom;
    w->geom_bytes = tg_geom_memsize(geom);
    w->owned = true;

    if (w->geom_bytes > 0) {
        rb_gc_adjust_memory_usage((ssize_t)w->geom_bytes);
    }

    rb_obj_freeze(wrapper);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static VALUE geom_wrap_borrowed(VALUE geom_owner, const struct tg_geom *geom) {
    tg_geom_wrapper_t *w;
    VALUE wrapper;

    if (!geom) {
        return Qnil;
    }

    wrapper = TypedData_Make_Struct(cTGGeometryGeom, tg_geom_wrapper_t, &tg_geom_type, w);
    w->geom_owner = geom_owner;
    w->geom = (struct tg_geom *)geom;
    w->geom_bytes = 0;
    w->owned = false;

    rb_obj_freeze(wrapper);
    RB_GC_GUARD(geom_owner);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

typedef struct {
    const char *ptr;
    long len;
} tg_string_copy_args_t;

static VALUE rb_str_new_from_c_copy(VALUE arg) {
    tg_string_copy_args_t *args = (tg_string_copy_args_t *)arg;
    return rb_str_new(args->ptr, args->len);
}

static void raise_geom_error_and_free_as(struct tg_geom *geom, VALUE exception_class,
                                         const char *nil_message, const char *too_large_message,
                                         const char *allocation_message) {
    const char *err;
    char *message_copy;
    size_t message_len;
    tg_string_copy_args_t string_args;
    VALUE message;
    int state = 0;

    if (!geom) {
        rb_raise(rb_eNoMemError, "%s", nil_message);
    }

    err = tg_geom_error(geom);
    if (!err) {
        return;
    }

    message_len = strlen(err);
    if (message_len > (size_t)LONG_MAX) {
        tg_geom_free(geom);
        rb_raise(rb_eNoMemError, "%s", too_large_message);
    }

    message_copy = (char *)malloc(message_len + 1);
    if (!message_copy) {
        tg_geom_free(geom);
        rb_raise(rb_eNoMemError, "%s", allocation_message);
    }

    memcpy(message_copy, err, message_len + 1);
    tg_geom_free(geom);

    string_args.ptr = message_copy;
    string_args.len = (long)message_len;
    message = rb_protect(rb_str_new_from_c_copy, (VALUE)&string_args, &state);
    free(message_copy);

    if (state) {
        rb_jump_tag(state);
    }

    rb_exc_raise(rb_exc_new_str(exception_class, message));
}

static VALUE raise_parse_error_from_geom(struct tg_geom *geom) {
    raise_geom_error_and_free_as(geom, eTGGeometryParseError, "TG geometry allocation failed",
                                 "parse error message is too large",
                                 "parse error message allocation failed");
    return Qnil;
}

static VALUE parse_string_with_format(VALUE input, enum tg_geometry_parse_format format,
                                      enum tg_index index) {
    struct tg_geom *geom = NULL;
    const char *data;
    size_t len;

    StringValue(input);
    data = RSTRING_PTR(input);
    len = (size_t)RSTRING_LEN(input);

    switch (format) {
    case TG_GEOMETRY_FORMAT_AUTO:
        geom = tg_parse_ix(data, len, index);
        break;
    case TG_GEOMETRY_FORMAT_GEOJSON:
        geom = tg_parse_geojsonn_ix(data, len, index);
        break;
    case TG_GEOMETRY_FORMAT_WKT:
        geom = tg_parse_wktn_ix(data, len, index);
        break;
    case TG_GEOMETRY_FORMAT_WKB:
        geom = tg_parse_wkb_ix((const uint8_t *)data, len, index);
        break;
    case TG_GEOMETRY_FORMAT_HEX:
        geom = tg_parse_hexn_ix(data, len, index);
        break;
    case TG_GEOMETRY_FORMAT_GEOBIN:
        geom = tg_parse_geobin_ix((const uint8_t *)data, len, index);
        break;
    }

    raise_parse_error_from_geom(geom);
    return geom_wrap_owned(geom);
}

static VALUE rb_tg_geometry_parse(int argc, VALUE *argv, VALUE self) {
    VALUE input;
    VALUE kwargs;
    VALUE format_value;
    VALUE index_value;
    enum tg_geometry_parse_format format;
    enum tg_index index;

    (void)self;
    rb_scan_args(argc, argv, "1:", &input, &kwargs);

    format_value = kwargs_value(kwargs, id_format, ID2SYM(id_auto));
    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    format = parse_format_symbol(format_value);
    index = parse_index_symbol(index_value);

    return parse_string_with_format(input, format, index);
}

static VALUE rb_tg_geometry_parse_geojson(int argc, VALUE *argv, VALUE self) {
    VALUE input;
    VALUE kwargs;
    VALUE index_value;
    enum tg_index index;

    (void)self;
    rb_scan_args(argc, argv, "1:", &input, &kwargs);

    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    index = parse_index_symbol(index_value);

    return parse_string_with_format(input, TG_GEOMETRY_FORMAT_GEOJSON, index);
}

static VALUE rb_tg_geometry_parse_wkt(int argc, VALUE *argv, VALUE self) {
    VALUE input;
    VALUE kwargs;
    VALUE index_value;
    enum tg_index index;

    (void)self;
    rb_scan_args(argc, argv, "1:", &input, &kwargs);

    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    index = parse_index_symbol(index_value);

    return parse_string_with_format(input, TG_GEOMETRY_FORMAT_WKT, index);
}

static VALUE rb_tg_geometry_parse_wkb(int argc, VALUE *argv, VALUE self) {
    VALUE input;
    VALUE kwargs;
    VALUE index_value;
    enum tg_index index;

    (void)self;
    rb_scan_args(argc, argv, "1:", &input, &kwargs);

    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    index = parse_index_symbol(index_value);

    return parse_string_with_format(input, TG_GEOMETRY_FORMAT_WKB, index);
}

static VALUE rb_tg_geometry_parse_hex(int argc, VALUE *argv, VALUE self) {
    VALUE input;
    VALUE kwargs;
    VALUE index_value;
    enum tg_index index;

    (void)self;
    rb_scan_args(argc, argv, "1:", &input, &kwargs);

    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    index = parse_index_symbol(index_value);

    return parse_string_with_format(input, TG_GEOMETRY_FORMAT_HEX, index);
}

static VALUE rb_tg_geometry_parse_geobin(int argc, VALUE *argv, VALUE self) {
    VALUE input;
    VALUE kwargs;
    VALUE index_value;
    enum tg_index index;

    (void)self;
    rb_scan_args(argc, argv, "1:", &input, &kwargs);

    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    index = parse_index_symbol(index_value);

    return parse_string_with_format(input, TG_GEOMETRY_FORMAT_GEOBIN, index);
}

static tg_geom_wrapper_t *get_geom_wrapper(VALUE value) {
    tg_geom_wrapper_t *w;
    TypedData_Get_Struct(value, tg_geom_wrapper_t, &tg_geom_type, w);

    if (!w || !w->geom) {
        rb_raise(rb_eArgError, "invalid TG::Geometry::Geom");
    }

    return w;
}

static void check_finite_double(double value, const char *name) {
    if (!isfinite(value)) {
        rb_raise(eTGGeometryArgumentError, "%s must be finite", name);
    }
}

static void validate_rect_coordinates(double min_x, double min_y, double max_x, double max_y) {
    check_finite_double(min_x, "min_x");
    check_finite_double(min_y, "min_y");
    check_finite_double(max_x, "max_x");
    check_finite_double(max_y, "max_y");

    if (min_x > max_x || min_y > max_y) {
        rb_raise(eTGGeometryArgumentError, "rectangle min coordinates must be <= max coordinates");
    }
}

static VALUE wrap_constructed_geom(struct tg_geom *geom) {
    raise_geom_error_and_free_as(geom, eTGGeometryError, "TG geometry allocation failed",
                                 "TG geometry error message is too large",
                                 "TG geometry error message allocation failed");
    return geom_wrap_owned(geom);
}

static VALUE rb_tg_geometry_point(VALUE self, VALUE x_value, VALUE y_value) {
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);
    struct tg_point point;

    (void)self;
    check_finite_double(x, "x");
    check_finite_double(y, "y");

    point.x = x;
    point.y = y;
    return wrap_constructed_geom(tg_geom_new_point(point));
}

static VALUE rb_tg_geometry_point_z(VALUE self, VALUE x_value, VALUE y_value, VALUE z_value) {
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);
    double z = NUM2DBL(z_value);
    struct tg_point point;

    (void)self;
    check_finite_double(x, "x");
    check_finite_double(y, "y");
    check_finite_double(z, "z");

    point.x = x;
    point.y = y;
    return wrap_constructed_geom(tg_geom_new_point_z(point, z));
}

static VALUE rb_tg_geometry_point_m(VALUE self, VALUE x_value, VALUE y_value, VALUE m_value) {
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);
    double m = NUM2DBL(m_value);
    struct tg_point point;

    (void)self;
    check_finite_double(x, "x");
    check_finite_double(y, "y");
    check_finite_double(m, "m");

    point.x = x;
    point.y = y;
    return wrap_constructed_geom(tg_geom_new_point_m(point, m));
}

static VALUE rb_tg_geometry_point_zm(VALUE self, VALUE x_value, VALUE y_value, VALUE z_value,
                                     VALUE m_value) {
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);
    double z = NUM2DBL(z_value);
    double m = NUM2DBL(m_value);
    struct tg_point point;

    (void)self;
    check_finite_double(x, "x");
    check_finite_double(y, "y");
    check_finite_double(z, "z");
    check_finite_double(m, "m");

    point.x = x;
    point.y = y;
    return wrap_constructed_geom(tg_geom_new_point_zm(point, z, m));
}

static VALUE rb_tg_geometry_empty_point(VALUE self) {
    (void)self;
    return wrap_constructed_geom(tg_geom_new_point_empty());
}

static VALUE rb_tg_geometry_empty_linestring(VALUE self) {
    (void)self;
    return wrap_constructed_geom(tg_geom_new_linestring_empty());
}

static VALUE rb_tg_geometry_empty_polygon(VALUE self) {
    (void)self;
    return wrap_constructed_geom(tg_geom_new_polygon_empty());
}

static VALUE rb_tg_geometry_empty_multipoint(VALUE self) {
    (void)self;
    return wrap_constructed_geom(tg_geom_new_multipoint_empty());
}

static VALUE rb_tg_geometry_empty_multilinestring(VALUE self) {
    (void)self;
    return wrap_constructed_geom(tg_geom_new_multilinestring_empty());
}

static VALUE rb_tg_geometry_empty_multipolygon(VALUE self) {
    (void)self;
    return wrap_constructed_geom(tg_geom_new_multipolygon_empty());
}

static VALUE rb_tg_geometry_empty_geometrycollection(VALUE self) {
    (void)self;
    return wrap_constructed_geom(tg_geom_new_geometrycollection_empty());
}

static VALUE rect_build(double min_x, double min_y, double max_x, double max_y) {
    tg_rect_wrapper_t *rect_data;
    VALUE rect;

    validate_rect_coordinates(min_x, min_y, max_x, max_y);

    rect = TypedData_Make_Struct(cTGGeometryRect, tg_rect_wrapper_t, &tg_rect_type, rect_data);
    rect_data->min_x = min_x;
    rect_data->min_y = min_y;
    rect_data->max_x = max_x;
    rect_data->max_y = max_y;
    rect_data->initialized = true;

    rb_obj_freeze(rect);
    RB_GC_GUARD(rect);
    return rect;
}

static VALUE rect_from_tg_rect(struct tg_rect rect) {
    return rect_build(rect.min.x, rect.min.y, rect.max.x, rect.max.y);
}

static VALUE point_array_from_tg_point(struct tg_point point) {
    return rb_ary_new_from_args(2, rb_float_new(point.x), rb_float_new(point.y));
}

static VALUE segment_wrap_value(struct tg_segment segment) {
    tg_segment_wrapper_t *w;
    VALUE wrapper =
        TypedData_Make_Struct(cTGGeometrySegment, tg_segment_wrapper_t, &tg_segment_type, w);

    w->segment = segment;
    w->initialized = true;

    rb_obj_freeze(wrapper);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static tg_segment_wrapper_t *get_segment_wrapper(VALUE value) {
    tg_segment_wrapper_t *w;

    TypedData_Get_Struct(value, tg_segment_wrapper_t, &tg_segment_type, w);
    if (!w || !w->initialized) {
        rb_raise(eTGGeometryArgumentError, "invalid TG::Geometry::Segment");
    }

    return w;
}

static int checked_child_index(VALUE index_value, int count, const char *name) {
    long index = NUM2LONG(index_value);

    if (index < 0 || index >= count) {
        rb_raise(eTGGeometryArgumentError, "%s index out of range", name);
    }

    if (index > INT_MAX) {
        rb_raise(eTGGeometryArgumentError, "%s index out of range", name);
    }

    return (int)index;
}

static VALUE line_wrap_borrowed(VALUE geom_owner, const struct tg_line *line) {
    tg_line_wrapper_t *w;
    VALUE wrapper;

    if (!line) {
        return Qnil;
    }

    wrapper = TypedData_Make_Struct(cTGGeometryLine, tg_line_wrapper_t, &tg_line_type, w);
    w->geom_owner = geom_owner;
    w->line = line;

    rb_obj_freeze(wrapper);
    RB_GC_GUARD(geom_owner);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static VALUE ring_wrap_borrowed(VALUE geom_owner, const struct tg_ring *ring) {
    tg_ring_wrapper_t *w;
    VALUE wrapper;

    if (!ring) {
        return Qnil;
    }

    wrapper = TypedData_Make_Struct(cTGGeometryRing, tg_ring_wrapper_t, &tg_ring_type, w);
    w->geom_owner = geom_owner;
    w->ring = ring;

    rb_obj_freeze(wrapper);
    RB_GC_GUARD(geom_owner);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static VALUE polygon_wrap_borrowed(VALUE geom_owner, const struct tg_poly *poly) {
    tg_polygon_wrapper_t *w;
    VALUE wrapper;

    if (!poly) {
        return Qnil;
    }

    wrapper = TypedData_Make_Struct(cTGGeometryPolygon, tg_polygon_wrapper_t, &tg_polygon_type, w);
    w->geom_owner = geom_owner;
    w->poly = poly;

    rb_obj_freeze(wrapper);
    RB_GC_GUARD(geom_owner);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static tg_line_wrapper_t *get_line_wrapper(VALUE value) {
    tg_line_wrapper_t *w;

    TypedData_Get_Struct(value, tg_line_wrapper_t, &tg_line_type, w);
    if (!w || !w->line) {
        rb_raise(eTGGeometryArgumentError, "invalid TG::Geometry::Line");
    }

    return w;
}

static tg_ring_wrapper_t *get_ring_wrapper(VALUE value) {
    tg_ring_wrapper_t *w;

    TypedData_Get_Struct(value, tg_ring_wrapper_t, &tg_ring_type, w);
    if (!w || !w->ring) {
        rb_raise(eTGGeometryArgumentError, "invalid TG::Geometry::Ring");
    }

    return w;
}

static tg_polygon_wrapper_t *get_polygon_wrapper(VALUE value) {
    tg_polygon_wrapper_t *w;

    TypedData_Get_Struct(value, tg_polygon_wrapper_t, &tg_polygon_type, w);
    if (!w || !w->poly) {
        rb_raise(eTGGeometryArgumentError, "invalid TG::Geometry::Polygon");
    }

    return w;
}

static VALUE rb_tg_geometry_rect_alloc(VALUE klass) {
    tg_rect_wrapper_t *rect_data;
    VALUE rect = TypedData_Make_Struct(klass, tg_rect_wrapper_t, &tg_rect_type, rect_data);

    rect_data->initialized = false;
    return rect;
}

static tg_rect_wrapper_t *get_rect_wrapper(VALUE value) {
    tg_rect_wrapper_t *rect_data;

    TypedData_Get_Struct(value, tg_rect_wrapper_t, &tg_rect_type, rect_data);

    if (!rect_data || !rect_data->initialized) {
        rb_raise(eTGGeometryArgumentError, "uninitialized TG::Geometry::Rect");
    }

    return rect_data;
}

static bool rect_intersects_rect_data(const tg_rect_wrapper_t *a, const tg_rect_wrapper_t *b) {
    return a->min_x <= b->max_x && a->max_x >= b->min_x && a->min_y <= b->max_y &&
           a->max_y >= b->min_y;
}

static bool rect_contains_point_data(const tg_rect_wrapper_t *rect_data, double x, double y) {
    return x >= rect_data->min_x && x <= rect_data->max_x && y >= rect_data->min_y &&
           y <= rect_data->max_y;
}

static VALUE rb_tg_geometry_rect_initialize(int argc, VALUE *argv, VALUE self) {
    VALUE min_x_value;
    VALUE min_y_value;
    VALUE max_x_value;
    VALUE max_y_value;
    tg_rect_wrapper_t *rect_data;
    double min_x;
    double min_y;
    double max_x;
    double max_y;

    rb_check_arity(argc, 4, 4);
    rb_scan_args(argc, argv, "40", &min_x_value, &min_y_value, &max_x_value, &max_y_value);

    min_x = NUM2DBL(min_x_value);
    min_y = NUM2DBL(min_y_value);
    max_x = NUM2DBL(max_x_value);
    max_y = NUM2DBL(max_y_value);

    validate_rect_coordinates(min_x, min_y, max_x, max_y);

    TypedData_Get_Struct(self, tg_rect_wrapper_t, &tg_rect_type, rect_data);
    rect_data->min_x = min_x;
    rect_data->min_y = min_y;
    rect_data->max_x = max_x;
    rect_data->max_y = max_y;
    rect_data->initialized = true;

    rb_obj_freeze(self);
    return self;
}

static VALUE rb_tg_geometry_rect_min_x(VALUE self) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    return rb_float_new(rect_data->min_x);
}

static VALUE rb_tg_geometry_rect_min_y(VALUE self) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    return rb_float_new(rect_data->min_y);
}

static VALUE rb_tg_geometry_rect_max_x(VALUE self) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    return rb_float_new(rect_data->max_x);
}

static VALUE rb_tg_geometry_rect_max_y(VALUE self) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    return rb_float_new(rect_data->max_y);
}

static VALUE rb_tg_geometry_rect_center(VALUE self) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    double center_x = rect_data->min_x + ((rect_data->max_x - rect_data->min_x) / 2.0);
    double center_y = rect_data->min_y + ((rect_data->max_y - rect_data->min_y) / 2.0);

    return rb_ary_new_from_args(2, rb_float_new(center_x), rb_float_new(center_y));
}

static VALUE rb_tg_geometry_rect_intersects_p(VALUE self, VALUE other) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    tg_rect_wrapper_t *other_data = get_rect_wrapper(other);

    return rect_intersects_rect_data(rect_data, other_data) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_rect_contains_point_p(VALUE self, VALUE x_value, VALUE y_value) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);

    check_finite_double(x, "x");
    check_finite_double(y, "y");

    return rect_contains_point_data(rect_data, x, y) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_rect_expand_to_include(VALUE self, VALUE other) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    tg_rect_wrapper_t *other_data = get_rect_wrapper(other);
    double min_x = rect_data->min_x < other_data->min_x ? rect_data->min_x : other_data->min_x;
    double min_y = rect_data->min_y < other_data->min_y ? rect_data->min_y : other_data->min_y;
    double max_x = rect_data->max_x > other_data->max_x ? rect_data->max_x : other_data->max_x;
    double max_y = rect_data->max_y > other_data->max_y ? rect_data->max_y : other_data->max_y;

    return rect_build(min_x, min_y, max_x, max_y);
}

static VALUE rb_tg_geometry_rect_expand_to_include_point(VALUE self, VALUE x_value, VALUE y_value) {
    tg_rect_wrapper_t *rect_data = get_rect_wrapper(self);
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);
    double min_x;
    double min_y;
    double max_x;
    double max_y;

    check_finite_double(x, "x");
    check_finite_double(y, "y");

    min_x = rect_data->min_x < x ? rect_data->min_x : x;
    min_y = rect_data->min_y < y ? rect_data->min_y : y;
    max_x = rect_data->max_x > x ? rect_data->max_x : x;
    max_y = rect_data->max_y > y ? rect_data->max_y : y;

    return rect_build(min_x, min_y, max_x, max_y);
}

static VALUE rb_tg_geometry_geom_type(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);

    switch (tg_geom_typeof(w->geom)) {
    case TG_POINT:
        return ID2SYM(rb_intern("point"));
    case TG_LINESTRING:
        return ID2SYM(rb_intern("linestring"));
    case TG_POLYGON:
        return ID2SYM(rb_intern("polygon"));
    case TG_MULTIPOINT:
        return ID2SYM(rb_intern("multipoint"));
    case TG_MULTILINESTRING:
        return ID2SYM(rb_intern("multilinestring"));
    case TG_MULTIPOLYGON:
        return ID2SYM(rb_intern("multipolygon"));
    case TG_GEOMETRYCOLLECTION:
        return ID2SYM(rb_intern("geometrycollection"));
    default:
        return ID2SYM(rb_intern("unknown"));
    }
}

static VALUE rb_tg_geometry_geom_bbox(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return rect_from_tg_rect(tg_geom_rect(w->geom));
}

static VALUE rb_tg_geometry_geom_covers_xy_p(VALUE self, VALUE x_value, VALUE y_value) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);
    struct tg_geom *point;
    bool result;

    check_finite_double(x, "x");
    check_finite_double(y, "y");

    {
        struct tg_point tg_point = {x, y};
        point = tg_geom_new_point(tg_point);
    }
    if (!point) {
        rb_raise(rb_eNoMemError, "TG point geometry allocation failed");
    }

    if (tg_geom_error(point)) {
        raise_geom_error_and_free_as(point, eTGGeometryArgumentError,
                                     "TG point geometry allocation failed",
                                     "TG point geometry error message is too large",
                                     "TG point geometry error message allocation failed");
    }

    result = tg_geom_covers(w->geom, point);
    tg_geom_free(point);
    return result ? Qtrue : Qfalse;
}

static VALUE geom_binary_predicate(VALUE self, VALUE other,
                                   bool (*predicate)(const struct tg_geom *,
                                                     const struct tg_geom *)) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    tg_geom_wrapper_t *other_w = get_geom_wrapper(other);

    return predicate(w->geom, other_w->geom) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_geom_equals_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_equals);
}

static VALUE rb_tg_geometry_geom_contains_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_contains);
}

static VALUE rb_tg_geometry_geom_intersects_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_intersects);
}

static VALUE rb_tg_geometry_geom_disjoint_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_disjoint);
}

static VALUE rb_tg_geometry_geom_within_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_within);
}

static VALUE rb_tg_geometry_geom_covers_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_covers);
}

static VALUE rb_tg_geometry_geom_covered_by_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_coveredby);
}

static VALUE rb_tg_geometry_geom_touches_p(VALUE self, VALUE other) {
    return geom_binary_predicate(self, other, tg_geom_touches);
}

static VALUE rb_tg_geometry_geom_intersects_xy_p(VALUE self, VALUE x_value, VALUE y_value) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    double x = NUM2DBL(x_value);
    double y = NUM2DBL(y_value);

    check_finite_double(x, "x");
    check_finite_double(y, "y");

    return tg_geom_intersects_xy(w->geom, x, y) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_geom_intersects_rect_p(int argc, VALUE *argv, VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    struct tg_rect rect;

    if (argc == 1) {
        tg_rect_wrapper_t *rect_data = get_rect_wrapper(argv[0]);
        rect.min.x = rect_data->min_x;
        rect.min.y = rect_data->min_y;
        rect.max.x = rect_data->max_x;
        rect.max.y = rect_data->max_y;
    } else if (argc == 4) {
        double min_x = NUM2DBL(argv[0]);
        double min_y = NUM2DBL(argv[1]);
        double max_x = NUM2DBL(argv[2]);
        double max_y = NUM2DBL(argv[3]);

        validate_rect_coordinates(min_x, min_y, max_x, max_y);
        rect.min.x = min_x;
        rect.min.y = min_y;
        rect.max.x = max_x;
        rect.max.y = max_y;
    } else {
        rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 1 or 4)", argc);
    }

    return tg_geom_intersects_rect(w->geom, rect) ? Qtrue : Qfalse;
}

static VALUE text_writer_result(size_t (*writer)(const struct tg_geom *, char *, size_t),
                                const struct tg_geom *geom) {
    size_t required = writer(geom, NULL, 0);
    VALUE str;
    size_t written;

    if (required > (size_t)LONG_MAX - 1) {
        rb_raise(rb_eNoMemError, "serialized text output is too large");
    }

    str = rb_str_new(NULL, (long)(required + 1));
    written = writer(geom, RSTRING_PTR(str), required + 1);

    if (written != required) {
        rb_raise(eTGGeometryError, "TG text writer size changed during serialization");
    }

    rb_str_set_len(str, (long)required);
    rb_enc_associate(str, rb_utf8_encoding());
    RB_GC_GUARD(str);
    return str;
}

static VALUE rb_tg_geometry_geom_to_geojson(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return text_writer_result(tg_geom_geojson, w->geom);
}

static VALUE rb_tg_geometry_geom_to_wkt(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return text_writer_result(tg_geom_wkt, w->geom);
}

static VALUE binary_writer_result(size_t (*writer)(const struct tg_geom *, uint8_t *, size_t),
                                  const struct tg_geom *geom, const char *name) {
    size_t required = writer(geom, NULL, 0);
    VALUE str;
    size_t written;

    if (required > (size_t)LONG_MAX) {
        rb_raise(rb_eNoMemError, "serialized %s output is too large", name);
    }

    str = rb_str_new(NULL, (long)required);
    written = writer(geom, (uint8_t *)RSTRING_PTR(str), required);

    if (written != required) {
        rb_raise(eTGGeometryError, "TG %s writer size changed during serialization", name);
    }

    rb_enc_associate(str, rb_ascii8bit_encoding());
    RB_GC_GUARD(str);
    return str;
}

static VALUE rb_tg_geometry_geom_to_wkb(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return binary_writer_result(tg_geom_wkb, w->geom, "WKB");
}

static VALUE rb_tg_geometry_geom_to_hex(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return text_writer_result(tg_geom_hex, w->geom);
}

static VALUE rb_tg_geometry_geom_to_geobin(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return binary_writer_result(tg_geom_geobin, w->geom, "GeoBIN");
}

static VALUE rb_tg_geometry_geom_extra_json(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    const char *extra_json = tg_geom_extra_json(w->geom);
    VALUE str;

    if (!extra_json) {
        return Qnil;
    }

    str = rb_str_new_cstr(extra_json);
    rb_enc_associate(str, rb_utf8_encoding());
    RB_GC_GUARD(str);
    return str;
}

static VALUE rb_tg_geometry_geom_point(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);

    if (tg_geom_typeof(w->geom) != TG_POINT) {
        return Qnil;
    }

    return point_array_from_tg_point(tg_geom_point(w->geom));
}

static VALUE rb_tg_geometry_geom_line(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);

    if (tg_geom_typeof(w->geom) != TG_LINESTRING) {
        return Qnil;
    }

    return line_wrap_borrowed(self, tg_geom_line(w->geom));
}

static VALUE rb_tg_geometry_geom_polygon(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);

    if (tg_geom_typeof(w->geom) != TG_POLYGON) {
        return Qnil;
    }

    return polygon_wrap_borrowed(self, tg_geom_poly(w->geom));
}

static VALUE rb_tg_geometry_geom_feature_p(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return tg_geom_is_feature(w->geom) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_geom_feature_collection_p(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return tg_geom_is_featurecollection(w->geom) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_geom_empty_p(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return tg_geom_is_empty(w->geom) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_geom_dims(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return INT2NUM(tg_geom_dims(w->geom));
}

static VALUE rb_tg_geometry_geom_has_z_p(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return tg_geom_has_z(w->geom) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_geom_has_m_p(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return tg_geom_has_m(w->geom) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_geom_z(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    if (!tg_geom_has_z(w->geom)) {
        return Qnil;
    }
    return rb_float_new(tg_geom_z(w->geom));
}

static VALUE rb_tg_geometry_geom_m(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    if (!tg_geom_has_m(w->geom)) {
        return Qnil;
    }
    return rb_float_new(tg_geom_m(w->geom));
}

static VALUE rb_tg_geometry_geom_extra_coords(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int count = tg_geom_num_extra_coords(w->geom);
    const double *coords = tg_geom_extra_coords(w->geom);
    VALUE result = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(result, rb_float_new(coords[i]));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(result);
    return result;
}

static VALUE rb_tg_geometry_geom_num_points(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return INT2NUM(tg_geom_num_points(w->geom));
}

static VALUE rb_tg_geometry_geom_point_at(VALUE self, VALUE index_value) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int index = checked_child_index(index_value, tg_geom_num_points(w->geom), "geometry point");
    return point_array_from_tg_point(tg_geom_point_at(w->geom, index));
}

static VALUE rb_tg_geometry_geom_points(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int count = tg_geom_num_points(w->geom);
    VALUE points = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(points, point_array_from_tg_point(tg_geom_point_at(w->geom, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(points);
    return points;
}

static VALUE rb_tg_geometry_geom_num_lines(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return INT2NUM(tg_geom_num_lines(w->geom));
}

static VALUE rb_tg_geometry_geom_line_at(VALUE self, VALUE index_value) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int index = checked_child_index(index_value, tg_geom_num_lines(w->geom), "geometry line");
    return line_wrap_borrowed(self, tg_geom_line_at(w->geom, index));
}

static VALUE rb_tg_geometry_geom_lines(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int count = tg_geom_num_lines(w->geom);
    VALUE lines = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(lines, line_wrap_borrowed(self, tg_geom_line_at(w->geom, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(lines);
    return lines;
}

static VALUE rb_tg_geometry_geom_num_polygons(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return INT2NUM(tg_geom_num_polys(w->geom));
}

static VALUE rb_tg_geometry_geom_polygon_at(VALUE self, VALUE index_value) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int index = checked_child_index(index_value, tg_geom_num_polys(w->geom), "geometry polygon");
    return polygon_wrap_borrowed(self, tg_geom_poly_at(w->geom, index));
}

static VALUE rb_tg_geometry_geom_polygons(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int count = tg_geom_num_polys(w->geom);
    VALUE polygons = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(polygons, polygon_wrap_borrowed(self, tg_geom_poly_at(w->geom, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(polygons);
    return polygons;
}

static VALUE rb_tg_geometry_geom_num_geometries(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return INT2NUM(tg_geom_num_geometries(w->geom));
}

static VALUE rb_tg_geometry_geom_geometry_at(VALUE self, VALUE index_value) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int index = checked_child_index(index_value, tg_geom_num_geometries(w->geom),
                                    "geometry collection child");
    return geom_wrap_borrowed(self, tg_geom_geometry_at(w->geom, index));
}

static VALUE rb_tg_geometry_geom_geometries(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    int count = tg_geom_num_geometries(w->geom);
    VALUE geometries = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(geometries, geom_wrap_borrowed(self, tg_geom_geometry_at(w->geom, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(geometries);
    return geometries;
}

static VALUE rb_tg_geometry_line_bbox(VALUE self) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    VALUE rect = rect_from_tg_rect(tg_line_rect(w->line));

    RB_GC_GUARD(self);
    return rect;
}

static VALUE rb_tg_geometry_line_num_points(VALUE self) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    return INT2NUM(tg_line_num_points(w->line));
}

static VALUE rb_tg_geometry_line_point_at(VALUE self, VALUE index_value) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    int index = checked_child_index(index_value, tg_line_num_points(w->line), "line point");

    return point_array_from_tg_point(tg_line_point_at(w->line, index));
}

static VALUE rb_tg_geometry_line_points(VALUE self) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    int count = tg_line_num_points(w->line);
    VALUE points = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(points, point_array_from_tg_point(tg_line_point_at(w->line, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(points);
    return points;
}

static VALUE rb_tg_geometry_line_num_segments(VALUE self) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    return INT2NUM(tg_line_num_segments(w->line));
}

static VALUE rb_tg_geometry_line_segment_at(VALUE self, VALUE index_value) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    int index = checked_child_index(index_value, tg_line_num_segments(w->line), "line segment");

    return segment_wrap_value(tg_line_segment_at(w->line, index));
}

static VALUE rb_tg_geometry_line_segments(VALUE self) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    int count = tg_line_num_segments(w->line);
    VALUE segments = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(segments, segment_wrap_value(tg_line_segment_at(w->line, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(segments);
    return segments;
}

static VALUE rb_tg_geometry_line_length(VALUE self) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    return rb_float_new(tg_line_length(w->line));
}

static VALUE rb_tg_geometry_line_clockwise_p(VALUE self) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    return tg_line_clockwise(w->line) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_ring_bbox(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    VALUE rect = rect_from_tg_rect(tg_ring_rect(w->ring));

    RB_GC_GUARD(self);
    return rect;
}

static VALUE rb_tg_geometry_ring_num_points(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    return INT2NUM(tg_ring_num_points(w->ring));
}

static VALUE rb_tg_geometry_ring_point_at(VALUE self, VALUE index_value) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    int index = checked_child_index(index_value, tg_ring_num_points(w->ring), "ring point");

    return point_array_from_tg_point(tg_ring_point_at(w->ring, index));
}

static VALUE rb_tg_geometry_ring_points(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    int count = tg_ring_num_points(w->ring);
    VALUE points = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(points, point_array_from_tg_point(tg_ring_point_at(w->ring, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(points);
    return points;
}

static VALUE rb_tg_geometry_ring_num_segments(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    return INT2NUM(tg_ring_num_segments(w->ring));
}

static VALUE rb_tg_geometry_ring_segment_at(VALUE self, VALUE index_value) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    int index = checked_child_index(index_value, tg_ring_num_segments(w->ring), "ring segment");

    return segment_wrap_value(tg_ring_segment_at(w->ring, index));
}

static VALUE rb_tg_geometry_ring_segments(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    int count = tg_ring_num_segments(w->ring);
    VALUE segments = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(segments, segment_wrap_value(tg_ring_segment_at(w->ring, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(segments);
    return segments;
}

static VALUE rb_tg_geometry_ring_area(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    return rb_float_new(tg_ring_area(w->ring));
}

static VALUE rb_tg_geometry_ring_perimeter(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    return rb_float_new(tg_ring_perimeter(w->ring));
}

static VALUE rb_tg_geometry_ring_clockwise_p(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    return tg_ring_clockwise(w->ring) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_ring_convex_p(VALUE self) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    return tg_ring_convex(w->ring) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_polygon_bbox(VALUE self) {
    tg_polygon_wrapper_t *w = get_polygon_wrapper(self);
    VALUE rect = rect_from_tg_rect(tg_poly_rect(w->poly));

    RB_GC_GUARD(self);
    return rect;
}

static VALUE rb_tg_geometry_polygon_exterior_ring(VALUE self) {
    tg_polygon_wrapper_t *w = get_polygon_wrapper(self);
    VALUE ring = ring_wrap_borrowed(w->geom_owner, tg_poly_exterior(w->poly));

    RB_GC_GUARD(self);
    return ring;
}

static VALUE rb_tg_geometry_polygon_num_holes(VALUE self) {
    tg_polygon_wrapper_t *w = get_polygon_wrapper(self);
    return INT2NUM(tg_poly_num_holes(w->poly));
}

static VALUE rb_tg_geometry_polygon_hole_at(VALUE self, VALUE index_value) {
    tg_polygon_wrapper_t *w = get_polygon_wrapper(self);
    int index = checked_child_index(index_value, tg_poly_num_holes(w->poly), "polygon hole");
    VALUE ring = ring_wrap_borrowed(w->geom_owner, tg_poly_hole_at(w->poly, index));

    RB_GC_GUARD(self);
    return ring;
}

static VALUE rb_tg_geometry_polygon_holes(VALUE self) {
    tg_polygon_wrapper_t *w = get_polygon_wrapper(self);
    int count = tg_poly_num_holes(w->poly);
    VALUE holes = rb_ary_new_capa(count);

    for (int i = 0; i < count; i++) {
        rb_ary_push(holes, ring_wrap_borrowed(w->geom_owner, tg_poly_hole_at(w->poly, i)));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(holes);
    return holes;
}

static VALUE rb_tg_geometry_polygon_clockwise_p(VALUE self) {
    tg_polygon_wrapper_t *w = get_polygon_wrapper(self);
    return tg_poly_clockwise(w->poly) ? Qtrue : Qfalse;
}

static VALUE rb_tg_geometry_segment_a(VALUE self) {
    tg_segment_wrapper_t *w = get_segment_wrapper(self);
    return point_array_from_tg_point(w->segment.a);
}

static VALUE rb_tg_geometry_segment_b(VALUE self) {
    tg_segment_wrapper_t *w = get_segment_wrapper(self);
    return point_array_from_tg_point(w->segment.b);
}

static VALUE rb_tg_geometry_segment_points(VALUE self) {
    tg_segment_wrapper_t *w = get_segment_wrapper(self);
    return rb_ary_new_from_args(2, point_array_from_tg_point(w->segment.a),
                                point_array_from_tg_point(w->segment.b));
}

static VALUE rb_tg_geometry_segment_bbox(VALUE self) {
    tg_segment_wrapper_t *w = get_segment_wrapper(self);
    return rect_from_tg_rect(tg_segment_rect(w->segment));
}

static VALUE rb_tg_geometry_segment_intersects_p(VALUE self, VALUE other) {
    tg_segment_wrapper_t *w = get_segment_wrapper(self);
    tg_segment_wrapper_t *other_w = get_segment_wrapper(other);

    return tg_segment_intersects_segment(w->segment, other_w->segment) ? Qtrue : Qfalse;
}

static tg_index_t *get_index_wrapper(VALUE value) {
    tg_index_t *idx;

    TypedData_Get_Struct(value, tg_index_t, &tg_index_type, idx);

    if (!idx) {
        rb_raise(eTGGeometryArgumentError, "invalid TG::Geometry::Index");
    }

    return idx;
}

static void *tg_rtree_malloc(size_t size) {
    tg_index_t *owner = tg_current_rtree_owner;
    tg_rtree_alloc_header_t *header;

    if (!owner) {
        return NULL;
    }

#ifdef TG_DEBUG_TEST
    if (tg_debug_fail_rtree_alloc_countdown >= 0) {
        if (tg_debug_fail_rtree_alloc_countdown == 0) {
            tg_debug_fail_rtree_alloc_countdown = -1;
            return NULL;
        }
        tg_debug_fail_rtree_alloc_countdown--;
    }
#endif

    if (size > SIZE_MAX - sizeof(tg_rtree_alloc_header_t)) {
        return NULL;
    }

    header = (tg_rtree_alloc_header_t *)malloc(sizeof(tg_rtree_alloc_header_t) + size);
    if (!header) {
        return NULL;
    }

    header->owner = owner;
    header->size = size;

    owner->rtree_bytes += size;
    rb_gc_adjust_memory_usage((ssize_t)size);

    return (void *)(header + 1);
}

static void tg_rtree_free(void *ptr) {
    tg_rtree_alloc_header_t *header;
    tg_index_t *owner;

    if (!ptr) {
        return;
    }

    header = ((tg_rtree_alloc_header_t *)ptr) - 1;
    owner = header->owner;

    if (owner) {
        if (owner->rtree_bytes >= header->size) {
            owner->rtree_bytes -= header->size;
        } else {
            owner->rtree_bytes = 0;
        }
    }

    rb_gc_adjust_memory_usage(-(ssize_t)header->size);
    free(header);
}

typedef struct {
    tg_index_t *idx;
    tg_index_t *saved_owner;
} tg_rtree_build_args_t;

static VALUE rtree_build_body(VALUE arg) {
    tg_rtree_build_args_t *args = (tg_rtree_build_args_t *)arg;
    tg_index_t *idx = args->idx;

    if (idx->len == 0) {
        return Qnil;
    }

    tg_current_rtree_owner = idx;
    idx->rtree = rtree_new_with_allocator(tg_rtree_malloc, tg_rtree_free);
    if (!idx->rtree) {
        rb_raise(rb_eNoMemError, "rtree allocation failed");
    }

    for (long i = 0; i < idx->len; i++) {
        double min[2] = {idx->entries[i].bbox.min.x, idx->entries[i].bbox.min.y};
        double max[2] = {idx->entries[i].bbox.max.x, idx->entries[i].bbox.max.y};

        if (!rtree_insert(idx->rtree, min, max, &idx->entries[i])) {
            rb_raise(rb_eNoMemError, "rtree insert failed");
        }
    }

    return Qnil;
}

static VALUE rtree_build_ensure(VALUE arg) {
    tg_rtree_build_args_t *args = (tg_rtree_build_args_t *)arg;
    tg_current_rtree_owner = args->saved_owner;
    return Qnil;
}

static void index_build_rtree(tg_index_t *idx) {
    tg_rtree_build_args_t args;

    if (idx->len == 0) {
        return;
    }

    args.idx = idx;
    args.saved_owner = tg_current_rtree_owner;

    rb_ensure(rtree_build_body, (VALUE)&args, rtree_build_ensure, (VALUE)&args);
}

static struct tg_rect tg_rect_from_xyxy(double min_x, double min_y, double max_x, double max_y) {
    struct tg_rect rect;

    rect.min.x = min_x;
    rect.min.y = min_y;
    rect.max.x = max_x;
    rect.max.y = max_y;
    return rect;
}

static void tg_rect_to_arrays(struct tg_rect rect, double min[2], double max[2]) {
    min[0] = rect.min.x;
    min[1] = rect.min.y;
    max[0] = rect.max.x;
    max[1] = rect.max.y;
}

static struct tg_geom *tg_query_point_new(double x, double y) {
    struct tg_point point = {x, y};
    return tg_geom_new_point(point);
}

static void tg_query_point_raise_if_invalid(struct tg_geom *point) {
    if (!point) {
        rb_raise(rb_eNoMemError, "TG point geometry allocation failed");
    }

    if (tg_geom_error(point)) {
        tg_geom_free(point);
        rb_raise(eTGGeometryError, "TG point geometry error");
    }
}

static bool index_entry_matches_point(const tg_index_t *idx, const tg_index_entry_t *entry,
                                      const struct tg_geom *point) {
    switch (idx->predicate) {
    case TG_GEOMETRY_INDEX_PREDICATE_COVERS:
        return tg_geom_covers(entry->geom, point);
    case TG_GEOMETRY_INDEX_PREDICATE_CONTAINS:
        return tg_geom_contains(entry->geom, point);
    }

    return false;
}

static bool index_entry_bbox_intersects_point(const tg_index_entry_t *entry, double x, double y) {
    struct tg_point point = {x, y};
    return tg_rect_intersects_point(entry->bbox, point);
}

static unsigned char *alloc_match_marks_raw(long len) {
    unsigned char *marks;

    if (len <= 0) {
        return NULL;
    }

#ifdef TG_DEBUG_TEST
    if (tg_debug_fail_next_match_buffer_alloc) {
        tg_debug_fail_next_match_buffer_alloc = false;
        return NULL;
    }
#endif

    marks = (unsigned char *)calloc((size_t)len, sizeof(unsigned char));
    return marks;
}

static unsigned char *alloc_match_marks(long len) {
    unsigned char *marks = alloc_match_marks_raw(len);

    if (len <= 0) {
        return NULL;
    }

    if (!marks) {
        rb_raise(rb_eNoMemError, "match buffer allocation failed");
    }

    return marks;
}

typedef struct {
    unsigned char *marks;
    long len;
} tg_rtree_mark_args_t;

static bool rtree_mark_candidate_iter(const double *min, const double *max, const void *data,
                                      void *udata) {
    const tg_index_entry_t *entry = (const tg_index_entry_t *)data;
    tg_rtree_mark_args_t *args = (tg_rtree_mark_args_t *)udata;

    (void)min;
    (void)max;

    if (entry && entry->ordinal >= 0 && entry->ordinal < args->len) {
        args->marks[entry->ordinal] = 1;
    }

    return true;
}

static unsigned char *rtree_candidate_marks(tg_index_t *idx, struct tg_rect query_rect) {
    unsigned char *marks;
    tg_rtree_mark_args_t args;
    double min[2];
    double max[2];

    marks = alloc_match_marks(idx->len);
    if (idx->len == 0 || !idx->rtree) {
        return marks;
    }

    args.marks = marks;
    args.len = idx->len;
    tg_rect_to_arrays(query_rect, min, max);
    rtree_search(idx->rtree, min, max, rtree_mark_candidate_iter, &args);

    return marks;
}

static VALUE build_ids_from_marks(tg_index_t *idx, const unsigned char *marks) {
    VALUE result = rb_ary_new();

    for (long i = 0; i < idx->len; i++) {
        if (marks[i]) {
            rb_ary_push(result, idx->entries[i].id);
        }
    }

    return result;
}

typedef struct {
    tg_index_t *idx;
    unsigned char *marks;
} tg_build_ids_args_t;

static VALUE build_ids_from_marks_body(VALUE arg) {
    tg_build_ids_args_t *args = (tg_build_ids_args_t *)arg;
    return build_ids_from_marks(args->idx, args->marks);
}

static VALUE build_ids_from_marks_protected(tg_index_t *idx, unsigned char *marks) {
    tg_build_ids_args_t args;
    VALUE result;
    int state = 0;

    args.idx = idx;
    args.marks = marks;
    result = rb_protect(build_ids_from_marks_body, (VALUE)&args, &state);
    free(marks);

    if (state) {
        rb_jump_tag(state);
    }

    return result;
}

static VALUE index_find_covering_value(tg_index_t *idx, double lon, double lat) {
    struct tg_geom *point;
    VALUE result = Qnil;

    if (idx->len == 0) {
        return Qnil;
    }

    if (idx->strategy == TG_GEOMETRY_INDEX_STRATEGY_RTREE) {
        struct tg_rect point_rect = tg_rect_from_xyxy(lon, lat, lon, lat);
        unsigned char *candidates = rtree_candidate_marks(idx, point_rect);

        point = tg_query_point_new(lon, lat);
        if (!point) {
            free(candidates);
            rb_raise(rb_eNoMemError, "TG point geometry allocation failed");
        }
        if (tg_geom_error(point)) {
            tg_geom_free(point);
            free(candidates);
            rb_raise(eTGGeometryError, "TG point geometry error");
        }

        for (long i = 0; i < idx->len; i++) {
            tg_index_entry_t *entry = &idx->entries[i];

            if (!candidates[i]) {
                continue;
            }
            if (index_entry_matches_point(idx, entry, point)) {
                result = entry->id;
                break;
            }
        }

        tg_geom_free(point);
        free(candidates);
        return result;
    }

    point = tg_query_point_new(lon, lat);
    tg_query_point_raise_if_invalid(point);

    for (long i = 0; i < idx->len; i++) {
        tg_index_entry_t *entry = &idx->entries[i];

        if (!index_entry_bbox_intersects_point(entry, lon, lat)) {
            continue;
        }

        if (index_entry_matches_point(idx, entry, point)) {
            result = entry->id;
            break;
        }
    }

    tg_geom_free(point);
    return result;
}

static unsigned char *index_covering_marks(tg_index_t *idx, double lon, double lat) {
    unsigned char *marks;
    struct tg_geom *point;

    if (idx->len == 0) {
        return NULL;
    }

    if (idx->strategy == TG_GEOMETRY_INDEX_STRATEGY_RTREE) {
        struct tg_rect point_rect = tg_rect_from_xyxy(lon, lat, lon, lat);
        unsigned char *candidates = rtree_candidate_marks(idx, point_rect);

        marks = alloc_match_marks_raw(idx->len);
        if (!marks) {
            free(candidates);
            rb_raise(rb_eNoMemError, "match buffer allocation failed");
        }

        point = tg_query_point_new(lon, lat);
        if (!point) {
            free(candidates);
            free(marks);
            rb_raise(rb_eNoMemError, "TG point geometry allocation failed");
        }
        if (tg_geom_error(point)) {
            tg_geom_free(point);
            free(candidates);
            free(marks);
            rb_raise(eTGGeometryError, "TG point geometry error");
        }

        for (long i = 0; i < idx->len; i++) {
            tg_index_entry_t *entry = &idx->entries[i];

            if (candidates[i] && index_entry_matches_point(idx, entry, point)) {
                marks[i] = 1;
            }
        }

        tg_geom_free(point);
        free(candidates);
        return marks;
    }

    marks = alloc_match_marks(idx->len);
    point = tg_query_point_new(lon, lat);
    if (!point) {
        free(marks);
        rb_raise(rb_eNoMemError, "TG point geometry allocation failed");
    }
    if (tg_geom_error(point)) {
        tg_geom_free(point);
        free(marks);
        rb_raise(eTGGeometryError, "TG point geometry error");
    }

    for (long i = 0; i < idx->len; i++) {
        tg_index_entry_t *entry = &idx->entries[i];

        if (index_entry_bbox_intersects_point(entry, lon, lat) &&
            index_entry_matches_point(idx, entry, point)) {
            marks[i] = 1;
        }
    }

    tg_geom_free(point);
    return marks;
}

static unsigned char *index_intersecting_rect_marks(tg_index_t *idx, struct tg_rect query_rect) {
    unsigned char *marks;

    if (idx->len == 0) {
        return NULL;
    }

    if (idx->strategy == TG_GEOMETRY_INDEX_STRATEGY_RTREE) {
        unsigned char *candidates = rtree_candidate_marks(idx, query_rect);

        marks = alloc_match_marks_raw(idx->len);
        if (!marks) {
            free(candidates);
            rb_raise(rb_eNoMemError, "match buffer allocation failed");
        }

        for (long i = 0; i < idx->len; i++) {
            tg_index_entry_t *entry = &idx->entries[i];

            if (candidates[i] && tg_geom_intersects_rect(entry->geom, query_rect)) {
                marks[i] = 1;
            }
        }

        free(candidates);
        return marks;
    }

    marks = alloc_match_marks(idx->len);
    for (long i = 0; i < idx->len; i++) {
        tg_index_entry_t *entry = &idx->entries[i];

        if (tg_rect_intersects_rect(entry->bbox, query_rect) &&
            tg_geom_intersects_rect(entry->geom, query_rect)) {
            marks[i] = 1;
        }
    }

    return marks;
}

typedef struct {
    tg_index_t *idx;
    VALUE entries;
    enum tg_geometry_index_via via;
    enum tg_index geometry_index;
} tg_index_build_args_t;

static void index_expand_bbox(tg_index_t *idx, struct tg_rect bbox) {
    if (!idx->has_bbox) {
        idx->bbox = bbox;
        idx->has_bbox = true;
        return;
    }

    if (bbox.min.x < idx->bbox.min.x)
        idx->bbox.min.x = bbox.min.x;
    if (bbox.min.y < idx->bbox.min.y)
        idx->bbox.min.y = bbox.min.y;
    if (bbox.max.x > idx->bbox.max.x)
        idx->bbox.max.x = bbox.max.x;
    if (bbox.max.y > idx->bbox.max.y)
        idx->bbox.max.y = bbox.max.y;
}

static void raise_parse_error_and_free_owned_geom(struct tg_geom *geom) {
    const char *err;
    char *message_copy;
    size_t message_len;
    tg_string_copy_args_t string_args;
    VALUE message;
    int state = 0;

    if (!geom) {
        rb_raise(rb_eNoMemError, "TG geometry allocation failed");
    }

    err = tg_geom_error(geom);
    if (!err)
        return;

    message_len = strlen(err);
    if (message_len > LONG_MAX) {
        tg_geom_free(geom);
        rb_raise(rb_eNoMemError, "parse error message is too large");
    }

    message_copy = malloc(message_len + 1);
    if (!message_copy) {
        tg_geom_free(geom);
        rb_raise(rb_eNoMemError, "parse error message allocation failed");
    }

    memcpy(message_copy, err, message_len + 1);
    tg_geom_free(geom);

    string_args.ptr = message_copy;
    string_args.len = (long)message_len;
    message = rb_protect(rb_str_new_from_c_copy, (VALUE)&string_args, &state);
    free(message_copy);

    if (state) {
        rb_jump_tag(state);
    }

    rb_exc_raise(rb_exc_new_str(eTGGeometryParseError, message));
}

static void fill_owned_index_entry(tg_index_t *idx, long i, VALUE id, VALUE value,
                                   enum tg_geometry_index_via via, enum tg_index geometry_index) {
    tg_index_entry_t entry;
    struct tg_geom *geom;
    VALUE string_value = value;

    if (!RB_TYPE_P(value, T_STRING)) {
        rb_raise(rb_eTypeError, "entry object must be a String for via: :geojson or via: :wkb");
    }

    StringValue(string_value);

    switch (via) {
    case TG_GEOMETRY_INDEX_VIA_GEOJSON:
        geom = tg_parse_geojsonn_ix(RSTRING_PTR(string_value), (size_t)RSTRING_LEN(string_value),
                                    geometry_index);
        break;
    case TG_GEOMETRY_INDEX_VIA_WKB:
        geom = tg_parse_wkb_ix((const uint8_t *)RSTRING_PTR(string_value),
                               (size_t)RSTRING_LEN(string_value), geometry_index);
        break;
    case TG_GEOMETRY_INDEX_VIA_GEOM:
        rb_raise(eTGGeometryError, "internal owned entry build mode mismatch");
    }

    raise_parse_error_and_free_owned_geom(geom);

    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    entry.geom_owner = Qnil;
    entry.geom = geom;
    entry.bbox = tg_geom_rect(geom);
    entry.geom_bytes = tg_geom_memsize(geom);
    entry.ordinal = i;
    entry.owned = true;

    idx->entries[i] = entry;
    idx->initialized++;
    idx->owned_geom_bytes_total += entry.geom_bytes;
    if (entry.geom_bytes > 0) {
        rb_gc_adjust_memory_usage((ssize_t)entry.geom_bytes);
    }
    index_expand_bbox(idx, entry.bbox);

    RB_GC_GUARD(string_value);
}

static void fill_borrowed_index_entry(tg_index_t *idx, long i, VALUE id, VALUE value) {
    tg_index_entry_t entry;
    tg_geom_wrapper_t *geom_wrapper = get_geom_wrapper(value);
    struct tg_geom *borrowed_geom = geom_wrapper->geom;

    if (!borrowed_geom) {
        rb_raise(eTGGeometryArgumentError, "invalid TG::Geometry::Geom");
    }

    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    entry.geom_owner = value;
    entry.geom = borrowed_geom;
    entry.bbox = tg_geom_rect(borrowed_geom);
    entry.geom_bytes = 0;
    entry.ordinal = i;
    entry.owned = false;

    idx->entries[i] = entry;
    idx->initialized++;
    index_expand_bbox(idx, entry.bbox);

    RB_GC_GUARD(value);
}

static VALUE index_build_body(VALUE arg) {
    tg_index_build_args_t *args = (tg_index_build_args_t *)arg;
    tg_index_t *idx = args->idx;

    for (long i = 0; i < idx->len; i++) {
        VALUE pair = rb_ary_entry(args->entries, i);
        VALUE id;
        VALUE value;

        if (!RB_TYPE_P(pair, T_ARRAY)) {
            rb_raise(rb_eTypeError, "each entry must be a two-element Array");
        }

        if (RARRAY_LEN(pair) != 2) {
            rb_raise(eTGGeometryArgumentError, "each entry must contain exactly [id, object]");
        }

        id = rb_ary_entry(pair, 0);
        value = rb_ary_entry(pair, 1);

        if (NIL_P(id)) {
            rb_raise(eTGGeometryArgumentError, "id cannot be nil");
        }

        switch (args->via) {
        case TG_GEOMETRY_INDEX_VIA_GEOM:
            fill_borrowed_index_entry(idx, i, id, value);
            break;
        case TG_GEOMETRY_INDEX_VIA_GEOJSON:
        case TG_GEOMETRY_INDEX_VIA_WKB:
            fill_owned_index_entry(idx, i, id, value, args->via, args->geometry_index);
            break;
        }
    }

    if (idx->strategy == TG_GEOMETRY_INDEX_STRATEGY_RTREE) {
        index_build_rtree(idx);
    }

    return Qnil;
}

static VALUE rb_tg_geometry_index_build(int argc, VALUE *argv, VALUE klass) {
    VALUE entries_value;
    VALUE kwargs;
    VALUE via_value;
    VALUE strategy_value;
    VALUE predicate_value;
    VALUE geometry_index_value;
    enum tg_geometry_index_via via;
    enum tg_geometry_index_strategy strategy;
    enum tg_geometry_index_predicate predicate;
    enum tg_index geometry_index;
    long len;
    tg_index_t *idx;
    VALUE wrapper;
    tg_index_build_args_t args;
    int state = 0;

    rb_scan_args(argc, argv, "1:", &entries_value, &kwargs);

    if (!RB_TYPE_P(entries_value, T_ARRAY)) {
        rb_raise(rb_eTypeError, "entries must be Array");
    }

    via_value = required_kwargs_value(kwargs, id_via, "via:");
    strategy_value = required_kwargs_value(kwargs, id_strategy, "strategy:");
    predicate_value = kwargs_value(kwargs, id_predicate, ID2SYM(id_covers));
    geometry_index_value = kwargs_value(kwargs, id_geometry_index, ID2SYM(id_ystripes));

    via = parse_index_via_symbol(via_value);
    strategy = parse_index_strategy_symbol(strategy_value);
    predicate = parse_index_predicate_symbol(predicate_value);
    geometry_index = parse_index_symbol(geometry_index_value);

    len = RARRAY_LEN(entries_value);
    wrapper = TypedData_Make_Struct(klass, tg_index_t, &tg_index_type, idx);
    idx->len = len;
    idx->capacity = len;
    idx->initialized = 0;
    idx->strategy = strategy;
    idx->predicate = predicate;
    idx->rtree = NULL;
    idx->frozen = false;
    idx->has_bbox = false;

    if (len > 0) {
        if ((size_t)len > SIZE_MAX / sizeof(tg_index_entry_t)) {
            rb_raise(rb_eNoMemError, "entries allocation size overflow");
        }

#ifdef TG_DEBUG_TEST
        if (tg_debug_fail_next_entries_alloc) {
            tg_debug_fail_next_entries_alloc = false;
            rb_raise(rb_eNoMemError, "entries allocation failed");
        }
#endif

        idx->entries = calloc((size_t)len, sizeof(tg_index_entry_t));
        if (!idx->entries) {
            rb_raise(rb_eNoMemError, "entries allocation failed");
        }

        idx->entries_bytes = (size_t)len * sizeof(tg_index_entry_t);
        rb_gc_adjust_memory_usage((ssize_t)idx->entries_bytes);
    }

    args.idx = idx;
    args.entries = entries_value;
    args.via = via;
    args.geometry_index = geometry_index;

    rb_protect(index_build_body, (VALUE)&args, &state);

    if (state) {
        index_dispose(idx);
        RB_GC_GUARD(entries_value);
        RB_GC_GUARD(wrapper);
        rb_jump_tag(state);
    }

    if (idx->initialized != idx->len) {
        index_dispose(idx);
        rb_raise(eTGGeometryError, "internal index build initialization mismatch");
    }

    idx->frozen = true;
    rb_obj_freeze(wrapper);

    RB_GC_GUARD(entries_value);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static VALUE rb_tg_geometry_index_size(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);
    return LONG2NUM(idx->len);
}

static VALUE rb_tg_geometry_index_strategy(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);

    switch (idx->strategy) {
    case TG_GEOMETRY_INDEX_STRATEGY_FLAT:
        return ID2SYM(id_flat);
    case TG_GEOMETRY_INDEX_STRATEGY_RTREE:
        return ID2SYM(id_rtree);
    }

    return Qnil;
}

static VALUE rb_tg_geometry_index_predicate(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);

    switch (idx->predicate) {
    case TG_GEOMETRY_INDEX_PREDICATE_COVERS:
        return ID2SYM(id_covers);
    case TG_GEOMETRY_INDEX_PREDICATE_CONTAINS:
        return ID2SYM(id_contains);
    }

    return Qnil;
}

static VALUE rb_tg_geometry_index_bbox(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);

    if (!idx->has_bbox)
        return Qnil;

    return rect_from_tg_rect(idx->bbox);
}

static void parse_public_point_args(VALUE lon_value, VALUE lat_value, double *lon, double *lat) {
    *lon = NUM2DBL(lon_value);
    *lat = NUM2DBL(lat_value);
    check_finite_double(*lon, "lon");
    check_finite_double(*lat, "lat");
}

static VALUE rb_tg_geometry_index_find_covering(VALUE self, VALUE lon_value, VALUE lat_value) {
    tg_index_t *idx = get_index_wrapper(self);
    double lon;
    double lat;
    VALUE result;

    parse_public_point_args(lon_value, lat_value, &lon, &lat);
    result = index_find_covering_value(idx, lon, lat);

    RB_GC_GUARD(self);
    return result;
}

static VALUE rb_tg_geometry_index_covering_ids(VALUE self, VALUE lon_value, VALUE lat_value) {
    tg_index_t *idx = get_index_wrapper(self);
    double lon;
    double lat;
    unsigned char *marks;
    VALUE result;

    parse_public_point_args(lon_value, lat_value, &lon, &lat);
    marks = index_covering_marks(idx, lon, lat);
    result = build_ids_from_marks_protected(idx, marks);

    RB_GC_GUARD(self);
    return result;
}

static VALUE rb_tg_geometry_index_intersecting_rect(VALUE self, VALUE min_x_value,
                                                    VALUE min_y_value, VALUE max_x_value,
                                                    VALUE max_y_value) {
    tg_index_t *idx = get_index_wrapper(self);
    double min_x = NUM2DBL(min_x_value);
    double min_y = NUM2DBL(min_y_value);
    double max_x = NUM2DBL(max_x_value);
    double max_y = NUM2DBL(max_y_value);
    struct tg_rect query_rect;
    unsigned char *marks;
    VALUE result;

    validate_rect_coordinates(min_x, min_y, max_x, max_y);
    query_rect = tg_rect_from_xyxy(min_x, min_y, max_x, max_y);

    marks = index_intersecting_rect_marks(idx, query_rect);
    result = build_ids_from_marks_protected(idx, marks);

    RB_GC_GUARD(self);
    return result;
}

static VALUE rb_tg_geometry_index_covering_ids_batch_packed(VALUE self, VALUE input) {
    tg_index_t *idx = get_index_wrapper(self);
    const char *data;
    long byte_len;
    long count;
    VALUE result;

    if (!RB_TYPE_P(input, T_STRING)) {
        rb_raise(rb_eTypeError, "packed input must be String");
    }

    byte_len = RSTRING_LEN(input);
    if (byte_len % (long)(2 * sizeof(double)) != 0) {
        rb_raise(eTGGeometryArgumentError, "packed input length must be multiple of 16 bytes");
    }

    count = byte_len / (long)(2 * sizeof(double));
    result = rb_ary_new_capa(count);
    data = RSTRING_PTR(input);

    for (long i = 0; i < count; i++) {
        double lon;
        double lat;
        VALUE id;

        memcpy(&lon, data + (i * (long)(2 * sizeof(double))), sizeof(double));
        memcpy(&lat, data + (i * (long)(2 * sizeof(double))) + (long)sizeof(double),
               sizeof(double));

        check_finite_double(lon, "lon");
        check_finite_double(lat, "lat");

        id = index_find_covering_value(idx, lon, lat);
        rb_ary_push(result, id);
    }

    RB_GC_GUARD(input);
    RB_GC_GUARD(self);
    RB_GC_GUARD(result);
    return result;
}

#ifdef TG_DEBUG_TEST
static VALUE rb_tg_geometry_debug_reset_test_hooks(VALUE self) {
    (void)self;
    tg_debug_fail_next_entries_alloc = false;
    tg_debug_fail_rtree_alloc_countdown = -1;
    tg_debug_fail_next_match_buffer_alloc = false;
    return Qnil;
}

static VALUE rb_tg_geometry_debug_fail_next_entries_alloc(VALUE self) {
    (void)self;
    tg_debug_fail_next_entries_alloc = true;
    return Qnil;
}

static VALUE rb_tg_geometry_debug_fail_next_rtree_alloc(VALUE self) {
    (void)self;
    tg_debug_fail_rtree_alloc_countdown = 0;
    return Qnil;
}

static VALUE rb_tg_geometry_debug_fail_rtree_alloc_after(VALUE self, VALUE count_value) {
    long count;

    (void)self;
    count = NUM2LONG(count_value);
    if (count < 0) {
        rb_raise(eTGGeometryArgumentError, "count must be >= 0");
    }
    tg_debug_fail_rtree_alloc_countdown = count;
    return Qnil;
}

static VALUE rb_tg_geometry_debug_fail_next_match_buffer_alloc(VALUE self) {
    (void)self;
    tg_debug_fail_next_match_buffer_alloc = true;
    return Qnil;
}

static VALUE rb_tg_geometry_index_rtree_bytes_for_test(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);
    return ULL2NUM((unsigned long long)idx->rtree_bytes);
}

static VALUE rb_tg_geometry_index_entries_bytes_for_test(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);
    return ULL2NUM((unsigned long long)idx->entries_bytes);
}

static VALUE rb_tg_geometry_index_owned_geom_bytes_for_test(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);
    return ULL2NUM((unsigned long long)idx->owned_geom_bytes_total);
}

static VALUE rb_tg_geometry_index_initialized_entries_for_test(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);
    return LONG2NUM(idx->initialized);
}

static VALUE rb_tg_geometry_index_force_dispose_for_test(VALUE self) {
    tg_index_t *idx = get_index_wrapper(self);
    index_dispose(idx);
    return Qnil;
}
#endif

RUBY_FUNC_EXPORTED void Init_tg_geometry_ext_geometry_ext(void) {
    tg_geometry_vendor_header_sanity();

    id_format = rb_intern("format");
    id_index = rb_intern("index");
    id_auto = rb_intern("auto");
    id_geojson = rb_intern("geojson");
    id_wkt = rb_intern("wkt");
    id_wkb = rb_intern("wkb");
    id_hex = rb_intern("hex");
    id_geobin = rb_intern("geobin");
    id_default = rb_intern("default");
    id_none = rb_intern("none");
    id_natural = rb_intern("natural");
    id_ystripes = rb_intern("ystripes");
    id_via = rb_intern("via");
    id_strategy = rb_intern("strategy");
    id_predicate = rb_intern("predicate");
    id_geometry_index = rb_intern("geometry_index");
    id_geom = rb_intern("geom");
    id_flat = rb_intern("flat");
    id_rtree = rb_intern("rtree");
    id_covers = rb_intern("covers");
    id_contains = rb_intern("contains");

    mTG = rb_define_module("TG");
    mTGGeometry = rb_define_module_under(mTG, "Geometry");

    eTGGeometryError = rb_define_class_under(mTGGeometry, "Error", rb_eStandardError);
    eTGGeometryParseError = rb_define_class_under(mTGGeometry, "ParseError", eTGGeometryError);
    eTGGeometryArgumentError = rb_define_class_under(mTGGeometry, "ArgumentError", rb_eArgError);
    eTGGeometryFrozenIndexError =
        rb_define_class_under(mTGGeometry, "FrozenIndexError", eTGGeometryError);

    rb_define_singleton_method(mTGGeometry, "parse", rb_tg_geometry_parse, -1);
    rb_define_singleton_method(mTGGeometry, "parse_geojson", rb_tg_geometry_parse_geojson, -1);
    rb_define_singleton_method(mTGGeometry, "parse_wkt", rb_tg_geometry_parse_wkt, -1);
    rb_define_singleton_method(mTGGeometry, "parse_wkb", rb_tg_geometry_parse_wkb, -1);
    rb_define_singleton_method(mTGGeometry, "parse_hex", rb_tg_geometry_parse_hex, -1);
    rb_define_singleton_method(mTGGeometry, "parse_geobin", rb_tg_geometry_parse_geobin, -1);
    rb_define_singleton_method(mTGGeometry, "point", rb_tg_geometry_point, 2);
    rb_define_singleton_method(mTGGeometry, "point_z", rb_tg_geometry_point_z, 3);
    rb_define_singleton_method(mTGGeometry, "point_m", rb_tg_geometry_point_m, 3);
    rb_define_singleton_method(mTGGeometry, "point_zm", rb_tg_geometry_point_zm, 4);
    rb_define_singleton_method(mTGGeometry, "empty_point", rb_tg_geometry_empty_point, 0);
    rb_define_singleton_method(mTGGeometry, "empty_linestring", rb_tg_geometry_empty_linestring, 0);
    rb_define_singleton_method(mTGGeometry, "empty_polygon", rb_tg_geometry_empty_polygon, 0);
    rb_define_singleton_method(mTGGeometry, "empty_multipoint", rb_tg_geometry_empty_multipoint, 0);
    rb_define_singleton_method(mTGGeometry, "empty_multilinestring",
                               rb_tg_geometry_empty_multilinestring, 0);
    rb_define_singleton_method(mTGGeometry, "empty_multipolygon", rb_tg_geometry_empty_multipolygon,
                               0);
    rb_define_singleton_method(mTGGeometry, "empty_geometrycollection",
                               rb_tg_geometry_empty_geometrycollection, 0);

    cTGGeometryGeom = rb_define_class_under(mTGGeometry, "Geom", rb_cObject);
    rb_undef_alloc_func(cTGGeometryGeom);
    rb_define_method(cTGGeometryGeom, "type", rb_tg_geometry_geom_type, 0);
    rb_define_method(cTGGeometryGeom, "bbox", rb_tg_geometry_geom_bbox, 0);
    rb_define_method(cTGGeometryGeom, "covers_xy?", rb_tg_geometry_geom_covers_xy_p, 2);
    rb_define_method(cTGGeometryGeom, "intersects_xy?", rb_tg_geometry_geom_intersects_xy_p, 2);
    rb_define_method(cTGGeometryGeom, "equals?", rb_tg_geometry_geom_equals_p, 1);
    rb_define_method(cTGGeometryGeom, "contains?", rb_tg_geometry_geom_contains_p, 1);
    rb_define_method(cTGGeometryGeom, "intersects?", rb_tg_geometry_geom_intersects_p, 1);
    rb_define_method(cTGGeometryGeom, "disjoint?", rb_tg_geometry_geom_disjoint_p, 1);
    rb_define_method(cTGGeometryGeom, "within?", rb_tg_geometry_geom_within_p, 1);
    rb_define_method(cTGGeometryGeom, "covers?", rb_tg_geometry_geom_covers_p, 1);
    rb_define_method(cTGGeometryGeom, "covered_by?", rb_tg_geometry_geom_covered_by_p, 1);
    rb_define_method(cTGGeometryGeom, "touches?", rb_tg_geometry_geom_touches_p, 1);
    rb_define_method(cTGGeometryGeom, "intersects_rect?", rb_tg_geometry_geom_intersects_rect_p,
                     -1);
    rb_define_method(cTGGeometryGeom, "to_geojson", rb_tg_geometry_geom_to_geojson, 0);
    rb_define_method(cTGGeometryGeom, "to_wkt", rb_tg_geometry_geom_to_wkt, 0);
    rb_define_method(cTGGeometryGeom, "to_wkb", rb_tg_geometry_geom_to_wkb, 0);
    rb_define_method(cTGGeometryGeom, "to_hex", rb_tg_geometry_geom_to_hex, 0);
    rb_define_method(cTGGeometryGeom, "to_geobin", rb_tg_geometry_geom_to_geobin, 0);
    rb_define_method(cTGGeometryGeom, "extra_json", rb_tg_geometry_geom_extra_json, 0);
    rb_define_method(cTGGeometryGeom, "feature?", rb_tg_geometry_geom_feature_p, 0);
    rb_define_method(cTGGeometryGeom, "feature_collection?",
                     rb_tg_geometry_geom_feature_collection_p, 0);
    rb_define_method(cTGGeometryGeom, "empty?", rb_tg_geometry_geom_empty_p, 0);
    rb_define_method(cTGGeometryGeom, "dims", rb_tg_geometry_geom_dims, 0);
    rb_define_method(cTGGeometryGeom, "has_z?", rb_tg_geometry_geom_has_z_p, 0);
    rb_define_method(cTGGeometryGeom, "has_m?", rb_tg_geometry_geom_has_m_p, 0);
    rb_define_method(cTGGeometryGeom, "z", rb_tg_geometry_geom_z, 0);
    rb_define_method(cTGGeometryGeom, "m", rb_tg_geometry_geom_m, 0);
    rb_define_method(cTGGeometryGeom, "extra_coords", rb_tg_geometry_geom_extra_coords, 0);
    rb_define_method(cTGGeometryGeom, "point", rb_tg_geometry_geom_point, 0);
    rb_define_method(cTGGeometryGeom, "num_points", rb_tg_geometry_geom_num_points, 0);
    rb_define_method(cTGGeometryGeom, "point_at", rb_tg_geometry_geom_point_at, 1);
    rb_define_method(cTGGeometryGeom, "points", rb_tg_geometry_geom_points, 0);
    rb_define_method(cTGGeometryGeom, "line", rb_tg_geometry_geom_line, 0);
    rb_define_method(cTGGeometryGeom, "num_lines", rb_tg_geometry_geom_num_lines, 0);
    rb_define_method(cTGGeometryGeom, "line_at", rb_tg_geometry_geom_line_at, 1);
    rb_define_method(cTGGeometryGeom, "lines", rb_tg_geometry_geom_lines, 0);
    rb_define_method(cTGGeometryGeom, "polygon", rb_tg_geometry_geom_polygon, 0);
    rb_define_method(cTGGeometryGeom, "num_polygons", rb_tg_geometry_geom_num_polygons, 0);
    rb_define_method(cTGGeometryGeom, "polygon_at", rb_tg_geometry_geom_polygon_at, 1);
    rb_define_method(cTGGeometryGeom, "polygons", rb_tg_geometry_geom_polygons, 0);
    rb_define_method(cTGGeometryGeom, "num_geometries", rb_tg_geometry_geom_num_geometries, 0);
    rb_define_method(cTGGeometryGeom, "geometry_at", rb_tg_geometry_geom_geometry_at, 1);
    rb_define_method(cTGGeometryGeom, "geometries", rb_tg_geometry_geom_geometries, 0);

    cTGGeometryLine = rb_define_class_under(mTGGeometry, "Line", rb_cObject);
    rb_undef_alloc_func(cTGGeometryLine);
    rb_define_method(cTGGeometryLine, "bbox", rb_tg_geometry_line_bbox, 0);
    rb_define_method(cTGGeometryLine, "num_points", rb_tg_geometry_line_num_points, 0);
    rb_define_method(cTGGeometryLine, "point_at", rb_tg_geometry_line_point_at, 1);
    rb_define_method(cTGGeometryLine, "points", rb_tg_geometry_line_points, 0);
    rb_define_method(cTGGeometryLine, "num_segments", rb_tg_geometry_line_num_segments, 0);
    rb_define_method(cTGGeometryLine, "segment_at", rb_tg_geometry_line_segment_at, 1);
    rb_define_method(cTGGeometryLine, "segments", rb_tg_geometry_line_segments, 0);
    rb_define_method(cTGGeometryLine, "length", rb_tg_geometry_line_length, 0);
    rb_define_method(cTGGeometryLine, "clockwise?", rb_tg_geometry_line_clockwise_p, 0);

    cTGGeometryRing = rb_define_class_under(mTGGeometry, "Ring", rb_cObject);
    rb_undef_alloc_func(cTGGeometryRing);
    rb_define_method(cTGGeometryRing, "bbox", rb_tg_geometry_ring_bbox, 0);
    rb_define_method(cTGGeometryRing, "num_points", rb_tg_geometry_ring_num_points, 0);
    rb_define_method(cTGGeometryRing, "point_at", rb_tg_geometry_ring_point_at, 1);
    rb_define_method(cTGGeometryRing, "points", rb_tg_geometry_ring_points, 0);
    rb_define_method(cTGGeometryRing, "num_segments", rb_tg_geometry_ring_num_segments, 0);
    rb_define_method(cTGGeometryRing, "segment_at", rb_tg_geometry_ring_segment_at, 1);
    rb_define_method(cTGGeometryRing, "segments", rb_tg_geometry_ring_segments, 0);
    rb_define_method(cTGGeometryRing, "area", rb_tg_geometry_ring_area, 0);
    rb_define_method(cTGGeometryRing, "perimeter", rb_tg_geometry_ring_perimeter, 0);
    rb_define_method(cTGGeometryRing, "clockwise?", rb_tg_geometry_ring_clockwise_p, 0);
    rb_define_method(cTGGeometryRing, "convex?", rb_tg_geometry_ring_convex_p, 0);

    cTGGeometryPolygon = rb_define_class_under(mTGGeometry, "Polygon", rb_cObject);
    rb_undef_alloc_func(cTGGeometryPolygon);
    rb_define_method(cTGGeometryPolygon, "bbox", rb_tg_geometry_polygon_bbox, 0);
    rb_define_method(cTGGeometryPolygon, "exterior_ring", rb_tg_geometry_polygon_exterior_ring, 0);
    rb_define_method(cTGGeometryPolygon, "num_holes", rb_tg_geometry_polygon_num_holes, 0);
    rb_define_method(cTGGeometryPolygon, "hole_at", rb_tg_geometry_polygon_hole_at, 1);
    rb_define_method(cTGGeometryPolygon, "holes", rb_tg_geometry_polygon_holes, 0);
    rb_define_method(cTGGeometryPolygon, "clockwise?", rb_tg_geometry_polygon_clockwise_p, 0);

    cTGGeometrySegment = rb_define_class_under(mTGGeometry, "Segment", rb_cObject);
    rb_undef_alloc_func(cTGGeometrySegment);
    rb_define_method(cTGGeometrySegment, "a", rb_tg_geometry_segment_a, 0);
    rb_define_method(cTGGeometrySegment, "b", rb_tg_geometry_segment_b, 0);
    rb_define_method(cTGGeometrySegment, "points", rb_tg_geometry_segment_points, 0);
    rb_define_method(cTGGeometrySegment, "bbox", rb_tg_geometry_segment_bbox, 0);
    rb_define_method(cTGGeometrySegment, "intersects?", rb_tg_geometry_segment_intersects_p, 1);

    cTGGeometryRect = rb_define_class_under(mTGGeometry, "Rect", rb_cObject);
    rb_define_alloc_func(cTGGeometryRect, rb_tg_geometry_rect_alloc);
    rb_define_method(cTGGeometryRect, "initialize", rb_tg_geometry_rect_initialize, -1);
    rb_define_method(cTGGeometryRect, "min_x", rb_tg_geometry_rect_min_x, 0);
    rb_define_method(cTGGeometryRect, "min_y", rb_tg_geometry_rect_min_y, 0);
    rb_define_method(cTGGeometryRect, "max_x", rb_tg_geometry_rect_max_x, 0);
    rb_define_method(cTGGeometryRect, "max_y", rb_tg_geometry_rect_max_y, 0);
    rb_define_method(cTGGeometryRect, "center", rb_tg_geometry_rect_center, 0);
    rb_define_method(cTGGeometryRect, "intersects?", rb_tg_geometry_rect_intersects_p, 1);
    rb_define_method(cTGGeometryRect, "contains_point?", rb_tg_geometry_rect_contains_point_p, 2);
    rb_define_method(cTGGeometryRect, "expand_to_include", rb_tg_geometry_rect_expand_to_include,
                     1);
    rb_define_method(cTGGeometryRect, "expand_to_include_point",
                     rb_tg_geometry_rect_expand_to_include_point, 2);

    cTGGeometryIndex = rb_define_class_under(mTGGeometry, "Index", rb_cObject);
    rb_undef_alloc_func(cTGGeometryIndex);
    rb_define_singleton_method(cTGGeometryIndex, "build", rb_tg_geometry_index_build, -1);
    rb_define_method(cTGGeometryIndex, "find_covering", rb_tg_geometry_index_find_covering, 2);
    rb_define_method(cTGGeometryIndex, "covering_ids", rb_tg_geometry_index_covering_ids, 2);
    rb_define_method(cTGGeometryIndex, "intersecting_rect", rb_tg_geometry_index_intersecting_rect,
                     4);
    rb_define_method(cTGGeometryIndex, "covering_ids_batch_packed",
                     rb_tg_geometry_index_covering_ids_batch_packed, 1);
    rb_define_method(cTGGeometryIndex, "size", rb_tg_geometry_index_size, 0);
    rb_define_method(cTGGeometryIndex, "strategy", rb_tg_geometry_index_strategy, 0);
    rb_define_method(cTGGeometryIndex, "predicate", rb_tg_geometry_index_predicate, 0);
    rb_define_method(cTGGeometryIndex, "bbox", rb_tg_geometry_index_bbox, 0);

#ifdef TG_DEBUG_TEST
    rb_define_singleton_method(mTGGeometry, "_debug_reset_test_hooks!",
                               rb_tg_geometry_debug_reset_test_hooks, 0);
    rb_define_singleton_method(mTGGeometry, "_debug_fail_next_entries_alloc!",
                               rb_tg_geometry_debug_fail_next_entries_alloc, 0);
    rb_define_singleton_method(mTGGeometry, "_debug_fail_next_rtree_alloc!",
                               rb_tg_geometry_debug_fail_next_rtree_alloc, 0);
    rb_define_singleton_method(mTGGeometry, "_debug_fail_rtree_alloc_after!",
                               rb_tg_geometry_debug_fail_rtree_alloc_after, 1);
    rb_define_singleton_method(mTGGeometry, "_debug_fail_next_match_buffer_alloc!",
                               rb_tg_geometry_debug_fail_next_match_buffer_alloc, 0);
    rb_define_method(cTGGeometryIndex, "_rtree_bytes_for_test",
                     rb_tg_geometry_index_rtree_bytes_for_test, 0);
    rb_define_method(cTGGeometryIndex, "_entries_bytes_for_test",
                     rb_tg_geometry_index_entries_bytes_for_test, 0);
    rb_define_method(cTGGeometryIndex, "_owned_geom_bytes_for_test",
                     rb_tg_geometry_index_owned_geom_bytes_for_test, 0);
    rb_define_method(cTGGeometryIndex, "_initialized_entries_for_test",
                     rb_tg_geometry_index_initialized_entries_for_test, 0);
    rb_define_method(cTGGeometryIndex, "_force_dispose_for_test!",
                     rb_tg_geometry_index_force_dispose_for_test, 0);
#endif
}
