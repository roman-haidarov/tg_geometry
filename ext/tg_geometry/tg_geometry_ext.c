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
#include "ruby/thread.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "tg.h"
#include "rtree.h"
#include "json.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__GNUC__) || defined(__clang__)
#define TG_GEOMETRY_NORETURN __attribute__((noreturn))
#else
#define TG_GEOMETRY_NORETURN
#endif

static VALUE mTG;
static VALUE mTGGeometry;
static VALUE cTGGeometryGeom;
static VALUE cTGGeometryRect;
static VALUE cTGGeometryIndex;
static VALUE mTGGeometryFeatureSource;
static VALUE cTGGeometryLine;
static VALUE cTGGeometryRing;
static VALUE cTGGeometryPolygon;
static VALUE cTGGeometrySegment;
static VALUE cTGGeometryNearestSegment;
static VALUE eTGGeometryError;
static VALUE eTGGeometryParseError;
static VALUE eTGGeometryArgumentError;
static VALUE eTGGeometryFrozenIndexError;

typedef struct {
    VALUE geom_owner;
    struct tg_geom *geom;
    size_t geom_bytes;
    bool owned;
    bool has_srid;
    int srid;
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

typedef struct {
    struct tg_segment segment;
    long index;
    double distance;
    struct tg_point point;
    bool initialized;
} tg_nearest_segment_wrapper_t;

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

enum tg_geometry_geom_query_predicate {
    TG_GEOMETRY_GEOM_QUERY_INTERSECTS,
    TG_GEOMETRY_GEOM_QUERY_COVERS,
    TG_GEOMETRY_GEOM_QUERY_CONTAINS,
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
static ID id_srid;
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
static ID id_id;
static ID id_only;
static ID id_on_invalid;
static ID id_on_missing_id;
static ID id_report;
static ID id_max_errors;
static ID id_read;
static ID id_raise;
static ID id_skip;
static ID id_ordinal;
static ID id_polygon;
static ID id_multipolygon;
static ID id_point;
static ID id_linestring;
static ID id_multipoint;
static ID id_multilinestring;
static ID id_geometrycollection;
static ID id_exterior;
static ID id_holes;

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
    w->has_srid = false;
    w->srid = 0;

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

static void nearest_segment_free(void *ptr) {
    ruby_xfree(ptr);
}

static size_t nearest_segment_memsize(const void *ptr) {
    (void)ptr;
    return sizeof(tg_nearest_segment_wrapper_t);
}

static const rb_data_type_t tg_nearest_segment_type = {
    "TG::Geometry::NearestSegment",
    {
        NULL,
        nearest_segment_free,
        nearest_segment_memsize,
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

typedef struct {
    const ID *allowed;
    size_t count;
} tg_keyword_validation_args_t;

static bool kwargs_has_key(VALUE kwargs, ID key) {
    if (NIL_P(kwargs)) {
        return false;
    }

    return RTEST(rb_funcall(kwargs, rb_intern("key?"), 1, ID2SYM(key)));
}

static bool keyword_allowed_p(ID key, const ID *allowed, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (key == allowed[i]) {
            return true;
        }
    }

    return false;
}

static int validate_keyword_i(VALUE key, VALUE value, VALUE arg) {
    tg_keyword_validation_args_t *args = (tg_keyword_validation_args_t *)arg;

    (void)value;

    if (!SYMBOL_P(key)) {
        rb_raise(eTGGeometryArgumentError, "keyword must be a Symbol");
    }

    if (!keyword_allowed_p(SYM2ID(key), args->allowed, args->count)) {
        const char *name = rb_id2name(SYM2ID(key));
        rb_raise(eTGGeometryArgumentError, "unknown keyword: :%s", name ? name : "?");
    }

    return ST_CONTINUE;
}

static void validate_keywords(VALUE kwargs, const ID *allowed, size_t count) {
    tg_keyword_validation_args_t args;

    if (NIL_P(kwargs)) {
        return;
    }

    if (!RB_TYPE_P(kwargs, T_HASH)) {
        rb_raise(eTGGeometryArgumentError, "keywords must be a Hash");
    }

    args.allowed = allowed;
    args.count = count;
    rb_hash_foreach(kwargs, validate_keyword_i, (VALUE)&args);
}

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

static uint32_t tg_geometry_read_u32(const uint8_t *bytes, uint8_t byte_order) {
    if (byte_order == 0) {
        return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) |
               (uint32_t)bytes[3];
    }

    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void tg_geometry_write_u32(uint8_t *bytes, uint32_t value, uint8_t byte_order) {
    if (byte_order == 0) {
        bytes[0] = (uint8_t)((value >> 24) & 0xff);
        bytes[1] = (uint8_t)((value >> 16) & 0xff);
        bytes[2] = (uint8_t)((value >> 8) & 0xff);
        bytes[3] = (uint8_t)(value & 0xff);
        return;
    }

    bytes[0] = (uint8_t)(value & 0xff);
    bytes[1] = (uint8_t)((value >> 8) & 0xff);
    bytes[2] = (uint8_t)((value >> 16) & 0xff);
    bytes[3] = (uint8_t)((value >> 24) & 0xff);
}

static bool extract_ewkb_srid(const uint8_t *bytes, size_t len, bool *has_srid_out, int *srid_out) {
    uint8_t byte_order;
    uint32_t type;
    uint32_t srid;

    *has_srid_out = false;
    *srid_out = 0;

    if (len < 5) {
        return false;
    }

    byte_order = bytes[0];
    if (byte_order > 1) {
        return false;
    }

    type = tg_geometry_read_u32(bytes + 1, byte_order);
    if ((type & 0x20000000u) == 0) {
        return true;
    }

    if (len < 9) {
        return false;
    }

    srid = tg_geometry_read_u32(bytes + 5, byte_order);
    if (srid > (uint32_t)INT_MAX) {
        rb_raise(eTGGeometryParseError, "EWKB SRID is out of supported range");
    }

    *has_srid_out = true;
    *srid_out = (int)srid;
    return true;
}

static int tg_geometry_hex_digit(unsigned char ch) {
    if (ch >= '0' && ch <= '9')
        return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'f')
        return (int)(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F')
        return (int)(ch - 'A' + 10);
    return -1;
}

static bool extract_hex_ewkb_srid(const char *hex, size_t len, bool *has_srid_out, int *srid_out) {
    uint8_t header[9];
    size_t bytes_to_decode;

    *has_srid_out = false;
    *srid_out = 0;

    if (len < 10) {
        return false;
    }

    bytes_to_decode = len >= 18 ? 9 : 5;
    for (size_t i = 0; i < bytes_to_decode; i++) {
        int hi = tg_geometry_hex_digit((unsigned char)hex[i * 2]);
        int lo = tg_geometry_hex_digit((unsigned char)hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        header[i] = (uint8_t)((hi << 4) | lo);
    }

    return extract_ewkb_srid(header, bytes_to_decode, has_srid_out, srid_out);
}

static bool parse_srid_option(VALUE srid_value, int *srid_out) {
    if (NIL_P(srid_value)) {
        *srid_out = 0;
        return false;
    }

    if (!RB_INTEGER_TYPE_P(srid_value)) {
        rb_raise(eTGGeometryArgumentError, "srid: must be an Integer in range [0, 2^31 - 1]");
    }

    if (RTEST(rb_funcall(srid_value, rb_intern("<"), 1, INT2NUM(0))) ||
        RTEST(rb_funcall(srid_value, rb_intern(">"), 1, INT2NUM(INT_MAX)))) {
        rb_raise(eTGGeometryArgumentError, "srid: must be an Integer in range [0, 2^31 - 1]");
    }

    *srid_out = NUM2INT(srid_value);
    return true;
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

static VALUE geom_wrap_owned_with_srid(struct tg_geom *geom, bool has_srid, int srid) {
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
    w->has_srid = has_srid;
    w->srid = has_srid ? srid : 0;

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
    w->has_srid = false;
    w->srid = 0;

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
    bool has_srid = false;
    int srid = 0;

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
        extract_ewkb_srid((const uint8_t *)data, len, &has_srid, &srid);
        geom = tg_parse_wkb_ix((const uint8_t *)data, len, index);
        break;
    case TG_GEOMETRY_FORMAT_HEX:
        extract_hex_ewkb_srid(data, len, &has_srid, &srid);
        geom = tg_parse_hexn_ix(data, len, index);
        break;
    case TG_GEOMETRY_FORMAT_GEOBIN:
        geom = tg_parse_geobin_ix((const uint8_t *)data, len, index);
        break;
    }

    raise_parse_error_from_geom(geom);
    return geom_wrap_owned_with_srid(geom, has_srid, srid);
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
    {
        ID allowed[] = {id_format, id_index};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

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
    {
        ID allowed[] = {id_index};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

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
    {
        ID allowed[] = {id_index};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

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
    {
        ID allowed[] = {id_index};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

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
    {
        ID allowed[] = {id_index};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

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
    {
        ID allowed[] = {id_index};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

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

static VALUE wrap_constructed_geom_with_srid(struct tg_geom *geom, bool has_srid, int srid) {
    raise_geom_error_and_free_as(geom, eTGGeometryError, "TG geometry allocation failed",
                                 "TG geometry error message is too large",
                                 "TG geometry error message allocation failed");
    return geom_wrap_owned_with_srid(geom, has_srid, srid);
}

static VALUE wrap_constructed_geom(struct tg_geom *geom) {
    return wrap_constructed_geom_with_srid(geom, false, 0);
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

typedef struct {
    VALUE points_value;
    struct tg_point *points;
    long len;
    const char *label;
} tg_parse_points_args_t;

static VALUE parse_points_body(VALUE arg) {
    tg_parse_points_args_t *args = (tg_parse_points_args_t *)arg;

    for (long i = 0; i < args->len; i++) {
        VALUE pair = rb_ary_entry(args->points_value, i);
        VALUE x_value;
        VALUE y_value;
        double x;
        double y;

        if (!RB_TYPE_P(pair, T_ARRAY) || RARRAY_LEN(pair) != 2) {
            rb_raise(eTGGeometryArgumentError, "%s point %ld must be [x, y]", args->label, i);
        }

        x_value = rb_ary_entry(pair, 0);
        y_value = rb_ary_entry(pair, 1);
        x = NUM2DBL(x_value);
        y = NUM2DBL(y_value);
        if (!isfinite(x)) {
            rb_raise(eTGGeometryArgumentError, "%s point %ld x must be finite", args->label, i);
        }
        if (!isfinite(y)) {
            rb_raise(eTGGeometryArgumentError, "%s point %ld y must be finite", args->label, i);
        }

        args->points[i].x = x;
        args->points[i].y = y;
    }

    return Qnil;
}

static struct tg_point *parse_points_array(VALUE points_value, long *len_out, const char *label) {
    tg_parse_points_args_t args;
    int state = 0;

    if (!RB_TYPE_P(points_value, T_ARRAY)) {
        rb_raise(rb_eTypeError, "%s must be an Array", label);
    }

    args.len = RARRAY_LEN(points_value);
    args.points_value = points_value;
    args.label = label;
    args.points = NULL;

    if (args.len > INT_MAX) {
        rb_raise(eTGGeometryArgumentError, "%s has too many points", label);
    }

    if (args.len > 0) {
        args.points = (struct tg_point *)ruby_xcalloc((size_t)args.len, sizeof(struct tg_point));
    }

    rb_protect(parse_points_body, (VALUE)&args, &state);
    if (state) {
        ruby_xfree(args.points);
        rb_jump_tag(state);
    }

    *len_out = args.len;
    return args.points;
}

static struct tg_ring *build_ring_from_ruby(VALUE ring_value, enum tg_index index,
                                            const char *label, long min_points,
                                            const char *min_message, const char *closed_message) {
    struct tg_point *points;
    struct tg_ring *ring;
    long len;

    points = parse_points_array(ring_value, &len, label);
    if (len < min_points) {
        ruby_xfree(points);
        rb_raise(eTGGeometryArgumentError, "%s", min_message);
    }
    if (len <= 0 || points[0].x != points[len - 1].x || points[0].y != points[len - 1].y) {
        ruby_xfree(points);
        rb_raise(eTGGeometryArgumentError, "%s", closed_message);
    }

    ring = tg_ring_new_ix(points, (int)len, index);
    ruby_xfree(points);
    if (!ring) {
        rb_raise(rb_eNoMemError, "TG ring allocation failed");
    }

    return ring;
}

typedef struct {
    VALUE exterior_value;
    VALUE holes_value;
    enum tg_index index;
    struct tg_ring *exterior;
    struct tg_ring **holes;
    long nholes;
    struct tg_poly *poly;
} tg_build_poly_args_t;

static void build_poly_cleanup(tg_build_poly_args_t *args) {
    if (!args) {
        return;
    }

    if (args->exterior) {
        tg_ring_free(args->exterior);
        args->exterior = NULL;
    }

    if (args->holes) {
        for (long i = 0; i < args->nholes; i++) {
            if (args->holes[i]) {
                tg_ring_free(args->holes[i]);
                args->holes[i] = NULL;
            }
        }
        ruby_xfree(args->holes);
        args->holes = NULL;
    }

    args->nholes = 0;
}

static VALUE build_poly_body(VALUE arg) {
    tg_build_poly_args_t *args = (tg_build_poly_args_t *)arg;

    args->exterior = build_ring_from_ruby(args->exterior_value, args->index, "polygon exterior", 4,
                                          "polygon exterior ring requires at least 4 points",
                                          "polygon exterior ring is not closed");

    if (!NIL_P(args->holes_value) && !RB_TYPE_P(args->holes_value, T_ARRAY)) {
        rb_raise(rb_eTypeError, "holes: must be an Array");
    }

    args->nholes = NIL_P(args->holes_value) ? 0 : RARRAY_LEN(args->holes_value);
    if (args->nholes > INT_MAX) {
        rb_raise(eTGGeometryArgumentError, "polygon has too many holes");
    }

    if (args->nholes > 0) {
        args->holes =
            (struct tg_ring **)ruby_xcalloc((size_t)args->nholes, sizeof(struct tg_ring *));
        for (long i = 0; i < args->nholes; i++) {
            char label[64];
            char min_message[96];
            char closed_message[96];
            VALUE hole_value = rb_ary_entry(args->holes_value, i);
            snprintf(label, sizeof(label), "polygon hole %ld", i);
            snprintf(min_message, sizeof(min_message),
                     "polygon hole %ld requires at least 4 points", i);
            snprintf(closed_message, sizeof(closed_message), "polygon hole %ld is not closed", i);
            args->holes[i] = build_ring_from_ruby(hole_value, args->index, label, 4, min_message,
                                                  closed_message);
        }
    }

    args->poly =
        tg_poly_new(args->exterior, (const struct tg_ring *const *)args->holes, (int)args->nholes);
    if (!args->poly) {
        rb_raise(rb_eNoMemError, "TG polygon allocation failed");
    }

    return Qnil;
}

static struct tg_poly *build_poly_from_ruby(VALUE exterior_value, VALUE holes_value,
                                            enum tg_index index) {
    tg_build_poly_args_t args;
    int state = 0;

    args.exterior_value = exterior_value;
    args.holes_value = holes_value;
    args.index = index;
    args.exterior = NULL;
    args.holes = NULL;
    args.nholes = 0;
    args.poly = NULL;

    rb_protect(build_poly_body, (VALUE)&args, &state);
    build_poly_cleanup(&args);

    if (state) {
        rb_jump_tag(state);
    }

    return args.poly;
}

static VALUE rb_tg_geometry_line_string(int argc, VALUE *argv, VALUE self) {
    VALUE points_value;
    VALUE kwargs;
    VALUE index_value;
    VALUE srid_value;
    enum tg_index index;
    bool has_srid;
    int srid;
    struct tg_point *points;
    long len;
    struct tg_line *line;
    struct tg_geom *geom;

    (void)self;
    rb_scan_args(argc, argv, "1:", &points_value, &kwargs);
    {
        ID allowed[] = {id_index, id_srid};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_natural));
    srid_value = kwargs_value(kwargs, id_srid, Qnil);
    index = parse_index_symbol(index_value);
    has_srid = parse_srid_option(srid_value, &srid);

    points = parse_points_array(points_value, &len, "line_string");
    if (len < 2) {
        ruby_xfree(points);
        rb_raise(eTGGeometryArgumentError, "line_string requires at least 2 points, got %ld", len);
    }

    line = tg_line_new_ix(points, (int)len, index);
    ruby_xfree(points);
    if (!line) {
        rb_raise(rb_eNoMemError, "TG line allocation failed");
    }

    geom = tg_geom_new_linestring(line);
    tg_line_free(line);
    return wrap_constructed_geom_with_srid(geom, has_srid, srid);
}

static VALUE rb_tg_geometry_polygon(int argc, VALUE *argv, VALUE self) {
    VALUE exterior_value;
    VALUE kwargs;
    VALUE holes_value;
    VALUE index_value;
    VALUE srid_value;
    enum tg_index index;
    bool has_srid;
    int srid;
    struct tg_poly *poly;
    struct tg_geom *geom;

    (void)self;
    rb_scan_args(argc, argv, "1:", &exterior_value, &kwargs);
    {
        ID allowed[] = {id_holes, id_index, id_srid};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

    holes_value = kwargs_value(kwargs, id_holes, Qnil);
    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    srid_value = kwargs_value(kwargs, id_srid, Qnil);
    index = parse_index_symbol(index_value);
    has_srid = parse_srid_option(srid_value, &srid);

    poly = build_poly_from_ruby(exterior_value, holes_value, index);
    geom = tg_geom_new_polygon(poly);
    tg_poly_free(poly);

    return wrap_constructed_geom_with_srid(geom, has_srid, srid);
}

typedef struct {
    VALUE polygons_value;
    enum tg_index index;
    long npolys;
    struct tg_poly **polys;
    long built;
    struct tg_geom *geom;
} tg_build_multipolygon_args_t;

static void build_multipolygon_cleanup(tg_build_multipolygon_args_t *args) {
    if (!args || !args->polys) {
        return;
    }

    for (long i = 0; i < args->built; i++) {
        if (args->polys[i]) {
            tg_poly_free(args->polys[i]);
            args->polys[i] = NULL;
        }
    }

    ruby_xfree(args->polys);
    args->polys = NULL;
    args->built = 0;
}

static VALUE build_multipolygon_body(VALUE arg) {
    tg_build_multipolygon_args_t *args = (tg_build_multipolygon_args_t *)arg;

    args->polys = (struct tg_poly **)ruby_xcalloc((size_t)args->npolys, sizeof(struct tg_poly *));

    for (long i = 0; i < args->npolys; i++) {
        VALUE item = rb_ary_entry(args->polygons_value, i);
        VALUE exterior_value;
        VALUE holes_value = Qnil;

        if (RB_TYPE_P(item, T_HASH)) {
            ID allowed[] = {id_exterior, id_holes};
            validate_keywords(item, allowed, sizeof(allowed) / sizeof(allowed[0]));

            exterior_value = rb_hash_aref(item, ID2SYM(id_exterior));
            holes_value = kwargs_value(item, id_holes, Qnil);
            if (NIL_P(exterior_value)) {
                rb_raise(eTGGeometryArgumentError, "multi_polygon polygon %ld requires :exterior",
                         i);
            }
        } else if (RB_TYPE_P(item, T_ARRAY)) {
            exterior_value = item;
        } else {
            rb_raise(rb_eTypeError, "multi_polygon polygon %ld must be a Hash or Array", i);
        }

        args->polys[i] = build_poly_from_ruby(exterior_value, holes_value, args->index);
        args->built = i + 1;
    }

    args->geom =
        tg_geom_new_multipolygon((const struct tg_poly *const *)args->polys, (int)args->npolys);
    if (!args->geom) {
        rb_raise(rb_eNoMemError, "TG multipolygon allocation failed");
    }

    return Qnil;
}

static VALUE rb_tg_geometry_multi_polygon(int argc, VALUE *argv, VALUE self) {
    VALUE polygons_value;
    VALUE kwargs;
    VALUE index_value;
    VALUE srid_value;
    enum tg_index index;
    bool has_srid;
    int srid;
    long npolys;
    tg_build_multipolygon_args_t args;
    int state = 0;

    (void)self;
    rb_scan_args(argc, argv, "1:", &polygons_value, &kwargs);
    {
        ID allowed[] = {id_index, id_srid};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

    if (!RB_TYPE_P(polygons_value, T_ARRAY)) {
        rb_raise(rb_eTypeError, "polygons must be an Array");
    }

    index_value = kwargs_value(kwargs, id_index, ID2SYM(id_ystripes));
    srid_value = kwargs_value(kwargs, id_srid, Qnil);
    index = parse_index_symbol(index_value);
    has_srid = parse_srid_option(srid_value, &srid);

    npolys = RARRAY_LEN(polygons_value);
    if (npolys > INT_MAX) {
        rb_raise(eTGGeometryArgumentError, "multi_polygon has too many polygons");
    }

    if (npolys == 0) {
        return wrap_constructed_geom_with_srid(tg_geom_new_multipolygon_empty(), has_srid, srid);
    }

    args.polygons_value = polygons_value;
    args.index = index;
    args.npolys = npolys;
    args.polys = NULL;
    args.built = 0;
    args.geom = NULL;

    rb_protect(build_multipolygon_body, (VALUE)&args, &state);
    build_multipolygon_cleanup(&args);

    if (state) {
        rb_jump_tag(state);
    }

    RB_GC_GUARD(polygons_value);
    return wrap_constructed_geom_with_srid(args.geom, has_srid, srid);
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

static int parse_required_srid_value(VALUE srid_value) {
    int srid;

    if (NIL_P(srid_value) || !parse_srid_option(srid_value, &srid)) {
        rb_raise(eTGGeometryArgumentError, "srid: must be an Integer in range [0, 2^31 - 1]");
    }

    return srid;
}

static VALUE rb_tg_geometry_geom_srid(VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    return w->has_srid ? INT2NUM(w->srid) : Qnil;
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

typedef struct {
    long len;
} tg_str_alloc_args_t;

static VALUE tg_str_new_binary_body(VALUE arg) {
    tg_str_alloc_args_t *args = (tg_str_alloc_args_t *)arg;
    VALUE str = rb_str_new(NULL, args->len);
    rb_enc_associate(str, rb_ascii8bit_encoding());
    return str;
}

static VALUE rb_tg_geometry_geom_to_ewkb(int argc, VALUE *argv, VALUE self) {
    tg_geom_wrapper_t *w = get_geom_wrapper(self);
    VALUE kwargs;
    VALUE srid_value;
    VALUE out;
    tg_str_alloc_args_t str_args;
    uint8_t *plain_buf;
    uint8_t *out_buf;
    size_t required;
    size_t written;
    uint8_t byte_order;
    uint32_t type;
    int effective_srid;
    bool explicit_srid;
    int state = 0;

    rb_scan_args(argc, argv, "0:", &kwargs);
    {
        ID allowed[] = {id_srid};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }
    explicit_srid = kwargs_has_key(kwargs, id_srid);

    if (explicit_srid) {
        srid_value = rb_hash_aref(kwargs, ID2SYM(id_srid));
        effective_srid = parse_required_srid_value(srid_value);
    } else if (w->has_srid) {
        effective_srid = w->srid;
    } else {
        rb_raise(eTGGeometryArgumentError, "to_ewkb requires srid (geom has no srid metadata)");
    }

    required = tg_geom_wkb(w->geom, NULL, 0);
    if (required < 5) {
        rb_raise(eTGGeometryError, "TG WKB writer produced an invalid header");
    }
    if (required > (size_t)LONG_MAX - 4) {
        rb_raise(rb_eNoMemError, "serialized EWKB output is too large");
    }

    plain_buf = (uint8_t *)ruby_xmalloc(required);
    written = tg_geom_wkb(w->geom, plain_buf, required);
    if (written != required) {
        ruby_xfree(plain_buf);
        rb_raise(eTGGeometryError, "TG WKB writer size changed during serialization");
    }

    byte_order = plain_buf[0];
    if (byte_order > 1) {
        ruby_xfree(plain_buf);
        rb_raise(eTGGeometryError, "TG WKB writer produced invalid byte order");
    }

    type = tg_geometry_read_u32(plain_buf + 1, byte_order);
    if ((type & 0x20000000u) != 0) {
        ruby_xfree(plain_buf);
        rb_raise(eTGGeometryError, "TG WKB writer unexpectedly produced EWKB SRID flag");
    }
    type |= 0x20000000u;

    str_args.len = (long)(required + 4);
    out = rb_protect(tg_str_new_binary_body, (VALUE)&str_args, &state);
    if (state) {
        ruby_xfree(plain_buf);
        rb_jump_tag(state);
    }

    out_buf = (uint8_t *)RSTRING_PTR(out);
    out_buf[0] = byte_order;
    tg_geometry_write_u32(out_buf + 1, type, byte_order);
    tg_geometry_write_u32(out_buf + 5, (uint32_t)effective_srid, byte_order);
    memcpy(out_buf + 9, plain_buf + 5, required - 5);
    ruby_xfree(plain_buf);

    rb_obj_freeze(out);
    RB_GC_GUARD(out);
    return out;
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

typedef struct {
    struct tg_point query;
    struct tg_segment best_segment;
    long best_index;
    double best_distance;
    bool found;
} tg_nearest_segment_ctx_t;

static tg_nearest_segment_wrapper_t *get_nearest_segment_wrapper(VALUE value) {
    tg_nearest_segment_wrapper_t *w;

    TypedData_Get_Struct(value, tg_nearest_segment_wrapper_t, &tg_nearest_segment_type, w);
    if (!w || !w->initialized) {
        rb_raise(eTGGeometryArgumentError, "invalid TG::Geometry::NearestSegment");
    }

    return w;
}

static double point_to_segment_distance(struct tg_point p, struct tg_segment seg) {
    double ax = seg.a.x;
    double ay = seg.a.y;
    double bx = seg.b.x;
    double by = seg.b.y;
    double dx = bx - ax;
    double dy = by - ay;
    double len_sq = dx * dx + dy * dy;
    double t;
    double proj_x;
    double proj_y;

    if (len_sq == 0.0) {
        return hypot(p.x - ax, p.y - ay);
    }

    t = ((p.x - ax) * dx + (p.y - ay) * dy) / len_sq;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;

    proj_x = ax + t * dx;
    proj_y = ay + t * dy;
    return hypot(p.x - proj_x, p.y - proj_y);
}

static struct tg_point project_point_onto_segment(struct tg_point p, struct tg_segment seg) {
    double ax = seg.a.x;
    double ay = seg.a.y;
    double bx = seg.b.x;
    double by = seg.b.y;
    double dx = bx - ax;
    double dy = by - ay;
    double len_sq = dx * dx + dy * dy;
    double t;
    struct tg_point projected;

    if (len_sq == 0.0) {
        projected.x = ax;
        projected.y = ay;
        return projected;
    }

    t = ((p.x - ax) * dx + (p.y - ay) * dy) / len_sq;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;

    projected.x = ax + t * dx;
    projected.y = ay + t * dy;
    return projected;
}

static double nearest_rect_distance(struct tg_rect rect, int *more, void *udata) {
    tg_nearest_segment_ctx_t *ctx = (tg_nearest_segment_ctx_t *)udata;
    double dx = 0.0;
    double dy = 0.0;

    *more = 0;

    if (ctx->query.x < rect.min.x) {
        dx = rect.min.x - ctx->query.x;
    } else if (ctx->query.x > rect.max.x) {
        dx = ctx->query.x - rect.max.x;
    }

    if (ctx->query.y < rect.min.y) {
        dy = rect.min.y - ctx->query.y;
    } else if (ctx->query.y > rect.max.y) {
        dy = ctx->query.y - rect.max.y;
    }

    return hypot(dx, dy);
}

static double nearest_segment_distance(struct tg_segment seg, int *more, void *udata) {
    tg_nearest_segment_ctx_t *ctx = (tg_nearest_segment_ctx_t *)udata;
    *more = 0;
    return point_to_segment_distance(ctx->query, seg);
}

static bool nearest_segment_iter(struct tg_segment seg, double dist, int index, void *udata) {
    tg_nearest_segment_ctx_t *ctx = (tg_nearest_segment_ctx_t *)udata;

    if (!ctx->found) {
        ctx->best_segment = seg;
        ctx->best_index = (long)index;
        ctx->best_distance = dist;
        ctx->found = true;
        return false;
    }

    return true;
}

static VALUE nearest_segment_wrap_value(tg_nearest_segment_ctx_t *ctx) {
    tg_nearest_segment_wrapper_t *w;
    VALUE wrapper = TypedData_Make_Struct(cTGGeometryNearestSegment, tg_nearest_segment_wrapper_t,
                                          &tg_nearest_segment_type, w);

    w->segment = ctx->best_segment;
    w->index = ctx->best_index;
    w->distance = ctx->best_distance;
    w->point = project_point_onto_segment(ctx->query, ctx->best_segment);
    w->initialized = true;

    rb_obj_freeze(wrapper);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static VALUE rb_tg_geometry_line_nearest_segment(VALUE self, VALUE x_value, VALUE y_value) {
    tg_line_wrapper_t *w = get_line_wrapper(self);
    tg_nearest_segment_ctx_t ctx;
    bool ok;

    ctx.query.x = NUM2DBL(x_value);
    ctx.query.y = NUM2DBL(y_value);
    check_finite_double(ctx.query.x, "x");
    check_finite_double(ctx.query.y, "y");
    ctx.best_index = -1;
    ctx.best_distance = INFINITY;
    ctx.found = false;

    ok = tg_line_nearest_segment(w->line, nearest_rect_distance, nearest_segment_distance,
                                 nearest_segment_iter, &ctx);
    if (!ok) {
        rb_raise(rb_eNoMemError, "nearest segment search failed");
    }
    if (!ctx.found) {
        return Qnil;
    }

    RB_GC_GUARD(self);
    return nearest_segment_wrap_value(&ctx);
}

static VALUE rb_tg_geometry_ring_nearest_segment(VALUE self, VALUE x_value, VALUE y_value) {
    tg_ring_wrapper_t *w = get_ring_wrapper(self);
    tg_nearest_segment_ctx_t ctx;
    bool ok;

    ctx.query.x = NUM2DBL(x_value);
    ctx.query.y = NUM2DBL(y_value);
    check_finite_double(ctx.query.x, "x");
    check_finite_double(ctx.query.y, "y");
    ctx.best_index = -1;
    ctx.best_distance = INFINITY;
    ctx.found = false;

    ok = tg_ring_nearest_segment(w->ring, nearest_rect_distance, nearest_segment_distance,
                                 nearest_segment_iter, &ctx);
    if (!ok) {
        rb_raise(rb_eNoMemError, "nearest segment search failed");
    }
    if (!ctx.found) {
        return Qnil;
    }

    RB_GC_GUARD(self);
    return nearest_segment_wrap_value(&ctx);
}

static VALUE rb_tg_geometry_nearest_segment_segment(VALUE self) {
    tg_nearest_segment_wrapper_t *w = get_nearest_segment_wrapper(self);
    return segment_wrap_value(w->segment);
}

static VALUE rb_tg_geometry_nearest_segment_index(VALUE self) {
    tg_nearest_segment_wrapper_t *w = get_nearest_segment_wrapper(self);
    return LONG2NUM(w->index);
}

static VALUE rb_tg_geometry_nearest_segment_distance(VALUE self) {
    tg_nearest_segment_wrapper_t *w = get_nearest_segment_wrapper(self);
    return rb_float_new(w->distance);
}

static VALUE rb_tg_geometry_nearest_segment_point(VALUE self) {
    tg_nearest_segment_wrapper_t *w = get_nearest_segment_wrapper(self);
    return point_array_from_tg_point(w->point);
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

typedef struct {
    const tg_index_t *idx;
    const struct tg_geom *point;
    const tg_index_entry_t *best;
    long best_ordinal;
} tg_rtree_find_args_t;

static bool rtree_find_covering_iter(const double *min, const double *max, const void *data,
                                     void *udata) {
    const tg_index_entry_t *entry = (const tg_index_entry_t *)data;
    tg_rtree_find_args_t *args = (tg_rtree_find_args_t *)udata;

    (void)min;
    (void)max;

    if (!entry) {
        return true;
    }

    if (entry->ordinal < 0 || entry->ordinal >= args->idx->len) {
        return true;
    }

    if (args->best && entry->ordinal >= args->best_ordinal) {
        return true;
    }
    if (index_entry_matches_point(args->idx, entry, args->point)) {
        args->best = entry;
        args->best_ordinal = entry->ordinal;

        if (entry->ordinal == 0) {
            return false;
        }
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
        tg_rtree_find_args_t args;
        double min[2];
        double max[2];

        point = tg_query_point_new(lon, lat);
        tg_query_point_raise_if_invalid(point);

        if (!idx->rtree) {
            tg_geom_free(point);
            return Qnil;
        }

        args.idx = idx;
        args.point = point;
        args.best = NULL;
        args.best_ordinal = 0;

        tg_rect_to_arrays(point_rect, min, max);
        rtree_search(idx->rtree, min, max, rtree_find_covering_iter, &args);

        if (args.best) {
            result = args.best->id;
        }

        tg_geom_free(point);
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

static bool index_geom_query_matches(const struct tg_geom *stored_geom,
                                     const struct tg_geom *query_geom,
                                     enum tg_geometry_geom_query_predicate predicate) {
    switch (predicate) {
    case TG_GEOMETRY_GEOM_QUERY_INTERSECTS:
        return tg_geom_intersects(stored_geom, query_geom);
    case TG_GEOMETRY_GEOM_QUERY_COVERS:
        return tg_geom_covers(stored_geom, query_geom);
    case TG_GEOMETRY_GEOM_QUERY_CONTAINS:
        return tg_geom_contains(stored_geom, query_geom);
    }

    return false;
}

enum tg_index_geom_query_status {
    TG_INDEX_GEOM_QUERY_OK = 0,
    TG_INDEX_GEOM_QUERY_MATCH_ALLOC_FAILED,
    TG_INDEX_GEOM_QUERY_RESULT_OVERFLOW,
    TG_INDEX_GEOM_QUERY_RESULT_ALLOC_FAILED,
};

static const char *index_geom_query_status_message(enum tg_index_geom_query_status status) {
    switch (status) {
    case TG_INDEX_GEOM_QUERY_MATCH_ALLOC_FAILED:
        return "match buffer allocation failed";
    case TG_INDEX_GEOM_QUERY_RESULT_OVERFLOW:
        return "result buffer allocation size overflow";
    case TG_INDEX_GEOM_QUERY_RESULT_ALLOC_FAILED:
        return "result buffer allocation failed";
    case TG_INDEX_GEOM_QUERY_OK:
        return "ok";
    }

    return "geometry query failed";
}

static enum tg_index_geom_query_status rtree_candidate_marks_no_raise(tg_index_t *idx,
                                                                      struct tg_rect query_rect,
                                                                      unsigned char **marks_out) {
    unsigned char *marks;
    tg_rtree_mark_args_t args;
    double min[2];
    double max[2];

    *marks_out = NULL;

    if (idx->len <= 0) {
        return TG_INDEX_GEOM_QUERY_OK;
    }

    marks = alloc_match_marks_raw(idx->len);
    if (!marks) {
        return TG_INDEX_GEOM_QUERY_MATCH_ALLOC_FAILED;
    }

    if (idx->rtree) {
        args.marks = marks;
        args.len = idx->len;
        tg_rect_to_arrays(query_rect, min, max);
        rtree_search(idx->rtree, min, max, rtree_mark_candidate_iter, &args);
    }

    *marks_out = marks;
    return TG_INDEX_GEOM_QUERY_OK;
}

static enum tg_index_geom_query_status
index_geom_query_collect(tg_index_t *idx, const struct tg_geom *query_geom,
                         struct tg_rect query_rect, enum tg_geometry_geom_query_predicate predicate,
                         long **indices_out, long *count_out) {
    unsigned char *marks = NULL;
    long *indices = NULL;
    long count = 0;
    enum tg_index_geom_query_status status;

    *indices_out = NULL;
    *count_out = 0;

    if (idx->strategy == TG_GEOMETRY_INDEX_STRATEGY_RTREE) {
        status = rtree_candidate_marks_no_raise(idx, query_rect, &marks);
        if (status != TG_INDEX_GEOM_QUERY_OK) {
            return status;
        }
    } else {
        marks = alloc_match_marks_raw(idx->len);
        if (!marks) {
            return TG_INDEX_GEOM_QUERY_MATCH_ALLOC_FAILED;
        }

        for (long i = 0; i < idx->len; i++) {
            if (tg_rect_intersects_rect(idx->entries[i].bbox, query_rect)) {
                marks[i] = 1;
            }
        }
    }

    if (idx->len < 0 || (size_t)idx->len > SIZE_MAX / sizeof(long)) {
        free(marks);
        return TG_INDEX_GEOM_QUERY_RESULT_OVERFLOW;
    }

    indices = (long *)calloc((size_t)idx->len, sizeof(long));
    if (!indices) {
        free(marks);
        return TG_INDEX_GEOM_QUERY_RESULT_ALLOC_FAILED;
    }

    for (long i = 0; i < idx->len; i++) {
        if (!marks[i]) {
            continue;
        }
        if (index_geom_query_matches(idx->entries[i].geom, query_geom, predicate)) {
            indices[count++] = i;
        }
    }

    free(marks);
    *indices_out = indices;
    *count_out = count;
    return TG_INDEX_GEOM_QUERY_OK;
}

typedef struct {
    tg_index_t *idx;
    long *indices;
    long count;
} tg_build_ids_from_indices_args_t;

static VALUE build_ids_from_indices_body(VALUE arg) {
    tg_build_ids_from_indices_args_t *args = (tg_build_ids_from_indices_args_t *)arg;
    VALUE result = rb_ary_new_capa(args->count);

    for (long i = 0; i < args->count; i++) {
        rb_ary_push(result, args->idx->entries[args->indices[i]].id);
    }

    return result;
}

static VALUE build_ids_from_indices_protected(tg_index_t *idx, long *indices, long count) {
    tg_build_ids_from_indices_args_t args;
    VALUE result;
    int state = 0;

    args.idx = idx;
    args.indices = indices;
    args.count = count;
    result = rb_protect(build_ids_from_indices_body, (VALUE)&args, &state);
    free(indices);

    if (state) {
        rb_jump_tag(state);
    }

    return result;
}

static VALUE index_geom_query_ids(VALUE self, VALUE geom_value,
                                  enum tg_geometry_geom_query_predicate predicate) {
    tg_index_t *idx = get_index_wrapper(self);
    tg_geom_wrapper_t *query_wrapper = get_geom_wrapper(geom_value);
    struct tg_geom *query_geom = query_wrapper->geom;
    struct tg_rect query_rect = tg_geom_rect(query_geom);
    long *indices = NULL;
    long count = 0;
    enum tg_index_geom_query_status status;

    if (idx->len == 0) {
        return rb_ary_new();
    }

    status = index_geom_query_collect(idx, query_geom, query_rect, predicate, &indices, &count);
    if (status != TG_INDEX_GEOM_QUERY_OK) {
        rb_raise(rb_eNoMemError, "%s", index_geom_query_status_message(status));
    }

    RB_GC_GUARD(self);
    RB_GC_GUARD(geom_value);
    return build_ids_from_indices_protected(idx, indices, count);
}

static VALUE rb_tg_geometry_index_intersecting_geom_ids(VALUE self, VALUE geom) {
    return index_geom_query_ids(self, geom, TG_GEOMETRY_GEOM_QUERY_INTERSECTS);
}

static VALUE rb_tg_geometry_index_covering_geom_ids(VALUE self, VALUE geom) {
    return index_geom_query_ids(self, geom, TG_GEOMETRY_GEOM_QUERY_COVERS);
}

static VALUE rb_tg_geometry_index_containing_geom_ids(VALUE self, VALUE geom) {
    return index_geom_query_ids(self, geom, TG_GEOMETRY_GEOM_QUERY_CONTAINS);
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
    {
        ID allowed[] = {id_via, id_strategy, id_predicate, id_geometry_index};
        validate_keywords(kwargs, allowed, sizeof(allowed) / sizeof(allowed[0]));
    }

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

typedef enum {
    FS_MODE_READ_ENTRIES,
    FS_MODE_READ_FEATURES,
    FS_MODE_BUILD_INDEX,
} fs_mode_t;

typedef enum {
    FS_ON_INVALID_RAISE,
    FS_ON_INVALID_SKIP,
} fs_on_invalid_t;

typedef enum {
    FS_ON_MISSING_ID_RAISE,
    FS_ON_MISSING_ID_SKIP,
    FS_ON_MISSING_ID_ORDINAL,
} fs_on_missing_id_t;

#define FS_GEOM_POINT              (1u << 0)
#define FS_GEOM_LINESTRING         (1u << 1)
#define FS_GEOM_POLYGON            (1u << 2)
#define FS_GEOM_MULTIPOINT         (1u << 3)
#define FS_GEOM_MULTILINESTRING    (1u << 4)
#define FS_GEOM_MULTIPOLYGON       (1u << 5)
#define FS_GEOM_GEOMETRYCOLLECTION (1u << 6)

typedef struct {
    VALUE id_path;
    bool only_all;
    unsigned int only_mask;
    fs_on_invalid_t on_invalid;
    fs_on_missing_id_t on_missing_id;
    bool report;
    long max_errors;
    enum tg_index geometry_index;
    enum tg_geometry_index_strategy strategy;
    enum tg_geometry_index_predicate predicate;
} fs_options_t;

typedef struct {
    char *data;
    size_t len;
} fs_source_t;

typedef struct {
    fs_source_t source;
    fs_options_t opts;
    fs_mode_t mode;
} fs_args_t;

typedef struct {
    fs_args_t *fs;
    struct json features;
    VALUE wrapper;
    tg_index_t *idx;
} fs_build_args_t;

typedef struct {
    bool accepted;
    bool filtered;
    bool missing_id_skip;
    VALUE id;
    struct json feature;
    struct json geometry;
    struct json properties;
    const char *reason;
    VALUE reason_value;
} fs_feature_result_t;

static VALUE fs_sym(const char *name) {
    return ID2SYM(rb_intern(name));
}

static VALUE fs_kwargs_value(VALUE kwargs, ID key, VALUE fallback, bool *present) {
    VALUE sym = ID2SYM(key);
    if (NIL_P(kwargs)) {
        if (present)
            *present = false;
        return fallback;
    }
    if (RTEST(rb_funcall(kwargs, rb_intern("key?"), 1, sym))) {
        if (present)
            *present = true;
        return rb_hash_aref(kwargs, sym);
    }
    if (present)
        *present = false;
    return fallback;
}

static VALUE fs_utf8_string(const char *ptr, size_t len) {
    VALUE str;

    if (len > LONG_MAX) {
        rb_raise(rb_eNoMemError, "FeatureSource JSON substring is too large");
    }

    str = rb_str_new(ptr, (long)len);
    rb_enc_associate(str, rb_utf8_encoding());
    return str;
}

static VALUE fs_cstr_utf8_string(const char *ptr) {
    VALUE str = rb_str_new_cstr(ptr);
    rb_enc_associate(str, rb_utf8_encoding());
    return str;
}

static size_t fs_json_offset(fs_source_t *source, struct json value) {
    const char *raw = json_raw(value);

    if (!raw || raw < source->data || raw > source->data + source->len) {
        return 0;
    }

    return (size_t)(raw - source->data);
}

static VALUE fs_error_hash(long feature_index, size_t byte_offset, VALUE reason) {
    VALUE h = rb_hash_new();
    rb_hash_aset(h, fs_sym("feature_index"), LONG2NUM(feature_index));
    rb_hash_aset(h, fs_sym("byte_offset"), ULL2NUM((unsigned long long)byte_offset));
    rb_hash_aset(h, fs_sym("reason"), reason);
    return h;
}

static void fs_report_error(VALUE errors, long max_errors, long feature_index, size_t byte_offset,
                            VALUE reason) {
    if (RARRAY_LEN(errors) < max_errors) {
        rb_ary_push(errors, fs_error_hash(feature_index, byte_offset, reason));
    }
}

static void TG_GEOMETRY_NORETURN fs_raise_parse_error_value(long feature_index, size_t byte_offset,
                                                            VALUE reason) {
    VALUE prefix =
        rb_sprintf("feature %ld at byte %llu: ", feature_index, (unsigned long long)byte_offset);
    VALUE message = rb_str_plus(prefix, reason);
    rb_exc_raise(rb_exc_new_str(eTGGeometryParseError, message));
}

static void TG_GEOMETRY_NORETURN fs_raise_argument_error(long feature_index, size_t byte_offset,
                                                         const char *reason) {
    rb_raise(eTGGeometryArgumentError, "feature %ld at byte %llu: %s", feature_index,
             (unsigned long long)byte_offset, reason);
}

static VALUE fs_copy_json_string_value(struct json value) {
    size_t len = json_string_copy(value, NULL, 0);
    VALUE str;

    if (len > LONG_MAX) {
        rb_raise(rb_eNoMemError, "JSON string is too large");
    }

    str = rb_str_new(NULL, (long)len + 1);
    json_string_copy(value, RSTRING_PTR(str), len + 1);
    rb_str_set_len(str, (long)len);
    rb_enc_associate(str, rb_utf8_encoding());
    return str;
}

static bool fs_json_number_is_integer(struct json value) {
    const char *raw = json_raw(value);
    size_t len = json_raw_length(value);

    if (!raw || len == 0)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '.' || raw[i] == 'e' || raw[i] == 'E')
            return false;
    }
    return true;
}

static bool fs_integer_id_from_json(struct json value, VALUE *id_out, VALUE *error_message) {
    const char *raw = json_raw(value);
    size_t len = json_raw_length(value);
    char stack[64];
    char *buf = stack;
    char *endp = NULL;
    long long parsed;

    if (!fs_json_number_is_integer(value)) {
        *error_message = rb_str_new_cstr("invalid id: numeric id must be an integer");
        return false;
    }

    if (len >= sizeof(stack)) {
        buf = (char *)malloc(len + 1);
        if (!buf)
            rb_raise(rb_eNoMemError, "id buffer allocation failed");
    }

    memcpy(buf, raw, len);
    buf[len] = '\0';
    errno = 0;
    parsed = strtoll(buf, &endp, 10);

    if (errno != 0 || endp != buf + len) {
        if (buf != stack)
            free(buf);
        *error_message = rb_str_new_cstr("invalid id: integer is out of range");
        return false;
    }

    *id_out = LL2NUM(parsed);
    if (buf != stack)
        free(buf);
    return true;
}

static bool fs_validate_integer_id_json(struct json value, VALUE *error_message) {
    const char *raw = json_raw(value);
    size_t len = json_raw_length(value);
    char stack[64];
    char *buf = stack;
    char *endp = NULL;

    if (!fs_json_number_is_integer(value)) {
        *error_message = rb_str_new_cstr("invalid id: numeric id must be an integer");
        return false;
    }

    if (len >= sizeof(stack)) {
        buf = (char *)malloc(len + 1);
        if (!buf)
            rb_raise(rb_eNoMemError, "id buffer allocation failed");
    }

    memcpy(buf, raw, len);
    buf[len] = '\0';
    errno = 0;
    (void)strtoll(buf, &endp, 10);

    if (errno != 0 || endp != buf + len) {
        if (buf != stack)
            free(buf);
        *error_message = rb_str_new_cstr("invalid id: integer is out of range");
        return false;
    }

    if (buf != stack)
        free(buf);
    return true;
}

static bool fs_id_from_json_value(struct json value, VALUE *id_out, VALUE *error_message,
                                  bool materialize_id) {
    switch (json_type(value)) {
    case JSON_STRING:
        *id_out = materialize_id ? fs_copy_json_string_value(value) : Qnil;
        return true;
    case JSON_NUMBER:
        if (!materialize_id) {
            *id_out = Qnil;
            return fs_validate_integer_id_json(value, error_message);
        }
        return fs_integer_id_from_json(value, id_out, error_message);
    case JSON_NULL:
    case JSON_TRUE:
    case JSON_FALSE:
    case JSON_ARRAY:
    case JSON_OBJECT:
        *error_message = rb_str_new_cstr("invalid id: expected JSON string or integer number");
        return false;
    }

    *error_message = rb_str_new_cstr("invalid id");
    return false;
}

static VALUE fs_default_id_path(void) {
    return rb_ary_new_from_args(2, fs_cstr_utf8_string("properties"), fs_cstr_utf8_string("@id"));
}

static VALUE fs_normalize_id_path(VALUE value) {
    VALUE path;

    if (NIL_P(value)) {
        return fs_default_id_path();
    }

    if (RB_TYPE_P(value, T_STRING)) {
        path = rb_funcall(value, rb_intern("split"), 1, rb_str_new_cstr("."));
    } else if (RB_TYPE_P(value, T_ARRAY)) {
        path = rb_ary_dup(value);
    } else {
        rb_raise(eTGGeometryArgumentError, "id: must be String or Array<String>");
    }

    if (RARRAY_LEN(path) == 0) {
        rb_raise(eTGGeometryArgumentError, "id: path cannot be empty");
    }

    for (long i = 0; i < RARRAY_LEN(path); i++) {
        VALUE part = rb_ary_entry(path, i);
        if (!RB_TYPE_P(part, T_STRING)) {
            rb_raise(eTGGeometryArgumentError, "id: every path component must be String");
        }
    }

    return path;
}

static bool fs_json_get_path(struct json root, VALUE path, struct json *out) {
    struct json cur = root;

    for (long i = 0; i < RARRAY_LEN(path); i++) {
        VALUE key = rb_ary_entry(path, i);
        StringValue(key);
        cur = json_object_get(cur, StringValueCStr(key));
        if (!json_exists(cur)) {
            *out = (struct json){0};
            return false;
        }
    }

    *out = cur;
    return true;
}

static unsigned int fs_geometry_type_bit(struct json type_value) {
    if (json_string_compare(type_value, "Point") == 0)
        return FS_GEOM_POINT;
    if (json_string_compare(type_value, "LineString") == 0)
        return FS_GEOM_LINESTRING;
    if (json_string_compare(type_value, "Polygon") == 0)
        return FS_GEOM_POLYGON;
    if (json_string_compare(type_value, "MultiPoint") == 0)
        return FS_GEOM_MULTIPOINT;
    if (json_string_compare(type_value, "MultiLineString") == 0)
        return FS_GEOM_MULTILINESTRING;
    if (json_string_compare(type_value, "MultiPolygon") == 0)
        return FS_GEOM_MULTIPOLYGON;
    if (json_string_compare(type_value, "GeometryCollection") == 0)
        return FS_GEOM_GEOMETRYCOLLECTION;
    return 0;
}

static unsigned int fs_symbol_geometry_type_bit(VALUE sym) {
    ID id;

    if (!SYMBOL_P(sym)) {
        rb_raise(eTGGeometryArgumentError, "only: must contain geometry type symbols");
    }

    id = SYM2ID(sym);
    if (id == id_point)
        return FS_GEOM_POINT;
    if (id == id_linestring)
        return FS_GEOM_LINESTRING;
    if (id == id_polygon)
        return FS_GEOM_POLYGON;
    if (id == id_multipoint)
        return FS_GEOM_MULTIPOINT;
    if (id == id_multilinestring)
        return FS_GEOM_MULTILINESTRING;
    if (id == id_multipolygon)
        return FS_GEOM_MULTIPOLYGON;
    if (id == id_geometrycollection)
        return FS_GEOM_GEOMETRYCOLLECTION;

    rb_raise(eTGGeometryArgumentError,
             "only: must contain one of :point, :linestring, :polygon, :multipoint, "
             ":multilinestring, :multipolygon, :geometrycollection");
}

static void fs_parse_only_option(fs_options_t *opts, VALUE value, bool present) {
    opts->only_all = false;
    opts->only_mask = FS_GEOM_POLYGON | FS_GEOM_MULTIPOLYGON;

    if (present && NIL_P(value)) {
        opts->only_all = true;
        opts->only_mask = 0;
        return;
    }

    if (!present) {
        return;
    }

    if (!RB_TYPE_P(value, T_ARRAY)) {
        rb_raise(eTGGeometryArgumentError, "only: must be Array<Symbol> or nil");
    }

    opts->only_mask = 0;
    for (long i = 0; i < RARRAY_LEN(value); i++) {
        opts->only_mask |= fs_symbol_geometry_type_bit(rb_ary_entry(value, i));
    }
}

static fs_on_invalid_t fs_parse_on_invalid(VALUE value) {
    ID id;
    if (!SYMBOL_P(value)) {
        rb_raise(eTGGeometryArgumentError, "on_invalid: must be one of :raise, :skip");
    }
    id = SYM2ID(value);
    if (id == id_raise)
        return FS_ON_INVALID_RAISE;
    if (id == id_skip)
        return FS_ON_INVALID_SKIP;
    rb_raise(eTGGeometryArgumentError, "on_invalid: must be one of :raise, :skip");
}

static fs_on_missing_id_t fs_parse_on_missing_id(VALUE value) {
    ID id;
    if (!SYMBOL_P(value)) {
        rb_raise(eTGGeometryArgumentError, "on_missing_id: must be one of :raise, :skip, :ordinal");
    }
    id = SYM2ID(value);
    if (id == id_raise)
        return FS_ON_MISSING_ID_RAISE;
    if (id == id_skip)
        return FS_ON_MISSING_ID_SKIP;
    if (id == id_ordinal)
        return FS_ON_MISSING_ID_ORDINAL;
    rb_raise(eTGGeometryArgumentError, "on_missing_id: must be one of :raise, :skip, :ordinal");
}

static bool fs_bool_option(VALUE value, const char *name) {
    if (value == Qtrue)
        return true;
    if (value == Qfalse)
        return false;
    rb_raise(eTGGeometryArgumentError, "%s must be true or false", name);
}

static bool fs_keyword_allowed(ID key_id, fs_mode_t mode) {
    if (key_id == id_id || key_id == id_only || key_id == id_on_invalid ||
        key_id == id_on_missing_id || key_id == id_geometry_index) {
        return true;
    }

    if (mode == FS_MODE_BUILD_INDEX) {
        return key_id == id_strategy || key_id == id_predicate || key_id == id_report;
    }

    return key_id == id_report || key_id == id_max_errors;
}

static int fs_validate_keyword_i(VALUE key, VALUE value, VALUE mode_value) {
    fs_mode_t mode = (fs_mode_t)NUM2INT(mode_value);
    (void)value;

    if (!SYMBOL_P(key) || !fs_keyword_allowed(SYM2ID(key), mode)) {
        VALUE inspected = rb_inspect(key);
        rb_raise(eTGGeometryArgumentError, "unknown keyword: %s", StringValueCStr(inspected));
    }

    return ST_CONTINUE;
}

static void fs_validate_keywords(VALUE kwargs, fs_mode_t mode) {
    if (NIL_P(kwargs))
        return;
    if (!RB_TYPE_P(kwargs, T_HASH)) {
        rb_raise(rb_eTypeError, "keywords must be a Hash");
    }

    rb_hash_foreach(kwargs, fs_validate_keyword_i, INT2FIX((int)mode));
}

static fs_options_t fs_parse_options(VALUE kwargs, fs_mode_t mode) {
    fs_options_t opts;
    VALUE id_value;
    VALUE only_value;
    VALUE on_invalid_value;
    VALUE on_missing_id_value;
    VALUE report_value;
    VALUE max_errors_value;
    VALUE geometry_index_value;
    VALUE strategy_value;
    VALUE predicate_value;
    bool only_present = false;

    memset(&opts, 0, sizeof(opts));
    fs_validate_keywords(kwargs, mode);

    id_value = fs_kwargs_value(kwargs, id_id, Qnil, NULL);
    only_value = fs_kwargs_value(kwargs, id_only, Qnil, &only_present);
    on_invalid_value = fs_kwargs_value(kwargs, id_on_invalid, ID2SYM(id_raise), NULL);
    on_missing_id_value = fs_kwargs_value(kwargs, id_on_missing_id, ID2SYM(id_raise), NULL);
    report_value = fs_kwargs_value(kwargs, id_report, Qfalse, NULL);
    max_errors_value = fs_kwargs_value(kwargs, id_max_errors, INT2NUM(100), NULL);
    geometry_index_value = fs_kwargs_value(kwargs, id_geometry_index, ID2SYM(id_ystripes), NULL);

    opts.id_path = fs_normalize_id_path(id_value);
    fs_parse_only_option(&opts, only_value, only_present);
    opts.on_invalid = fs_parse_on_invalid(on_invalid_value);
    opts.on_missing_id = fs_parse_on_missing_id(on_missing_id_value);
    opts.report = fs_bool_option(report_value, "report:");
    opts.max_errors = NUM2LONG(max_errors_value);
    if (opts.max_errors < 0) {
        rb_raise(eTGGeometryArgumentError, "max_errors: must be >= 0");
    }
    opts.geometry_index = parse_index_symbol(geometry_index_value);

    if (opts.on_invalid == FS_ON_INVALID_SKIP && !opts.report) {
        rb_raise(eTGGeometryArgumentError, "on_invalid: :skip requires report: true");
    }
    if (opts.on_missing_id == FS_ON_MISSING_ID_SKIP && !opts.report) {
        rb_raise(eTGGeometryArgumentError, "on_missing_id: :skip requires report: true");
    }

    if (mode == FS_MODE_BUILD_INDEX) {
        if (opts.report) {
            rb_raise(eTGGeometryArgumentError, "build_index_* does not accept report: true");
        }
        strategy_value = fs_kwargs_value(kwargs, id_strategy, ID2SYM(id_rtree), NULL);
        predicate_value = fs_kwargs_value(kwargs, id_predicate, ID2SYM(id_covers), NULL);
        opts.strategy = parse_index_strategy_symbol(strategy_value);
        opts.predicate = parse_index_predicate_symbol(predicate_value);
    } else {
        opts.strategy = TG_GEOMETRY_INDEX_STRATEGY_RTREE;
        opts.predicate = TG_GEOMETRY_INDEX_PREDICATE_COVERS;
    }

    return opts;
}

static VALUE fs_tg_parse_error_message(struct tg_geom *geom) {
    const char *err;
    VALUE message;

    if (!geom) {
        rb_raise(rb_eNoMemError, "TG geometry allocation failed");
    }

    err = tg_geom_error(geom);
    if (!err) {
        return Qnil;
    }

    message = rb_str_new_cstr(err);
    tg_geom_free(geom);
    return message;
}

static bool fs_validate_geometry_or_error(struct json geometry, enum tg_index geometry_index,
                                          VALUE *error_message) {
    struct tg_geom *geom =
        tg_parse_geojsonn_ix(json_raw(geometry), json_raw_length(geometry), geometry_index);
    VALUE message = fs_tg_parse_error_message(geom);

    if (!NIL_P(message)) {
        *error_message = message;
        return false;
    }

    tg_geom_free(geom);
    return true;
}

static VALUE fs_missing_id_ordinal(long feature_index) {
    return rb_sprintf("feature/%ld", feature_index);
}

static bool fs_extract_id(fs_source_t *source, fs_options_t *opts, struct json feature,
                          long feature_index, VALUE *id_out, VALUE *error_message,
                          bool materialize_id) {
    struct json id_json;

    if (!fs_json_get_path(feature, opts->id_path, &id_json) || json_type(id_json) == JSON_NULL) {
        switch (opts->on_missing_id) {
        case FS_ON_MISSING_ID_ORDINAL:
            *id_out = materialize_id ? fs_missing_id_ordinal(feature_index) : Qnil;
            return true;
        case FS_ON_MISSING_ID_SKIP:
            *error_message = rb_sprintf("missing id at configured path");
            return false;
        case FS_ON_MISSING_ID_RAISE:
            fs_raise_argument_error(feature_index, fs_json_offset(source, feature),
                                    "missing id at configured path");
        }
    }

    if (!fs_id_from_json_value(id_json, id_out, error_message, materialize_id)) {
        if (opts->on_invalid == FS_ON_INVALID_RAISE) {
            fs_raise_argument_error(feature_index, fs_json_offset(source, id_json),
                                    StringValueCStr(*error_message));
        }
        return false;
    }

    return true;
}

static bool fs_feature_prepare(fs_source_t *source, fs_options_t *opts, fs_mode_t mode,
                               struct json feature, long feature_index, bool materialize_id,
                               fs_feature_result_t *result) {
    struct json geometry;
    struct json geometry_type;
    struct json properties;
    unsigned int geom_bit;
    VALUE id = Qnil;
    VALUE error_message = Qnil;

    memset(result, 0, sizeof(*result));
    result->feature = feature;
    result->id = Qnil;
    result->reason_value = Qnil;

    if (!json_exists(feature) || json_type(feature) != JSON_OBJECT) {
        result->reason = "feature is not an object";
        return false;
    }

    geometry = json_object_get(feature, "geometry");
    if (!json_exists(geometry) || json_type(geometry) == JSON_NULL) {
        result->reason = "missing geometry";
        return false;
    }
    if (json_type(geometry) != JSON_OBJECT) {
        result->reason = "geometry must be an object";
        return false;
    }

    geometry_type = json_object_get(geometry, "type");
    if (!json_exists(geometry_type) || json_type(geometry_type) != JSON_STRING) {
        result->reason = "geometry.type must be a string";
        return false;
    }

    geom_bit = fs_geometry_type_bit(geometry_type);
    if (geom_bit == 0) {
        result->reason = "unsupported geometry.type";
        return false;
    }

    if (!opts->only_all && (opts->only_mask & geom_bit) == 0) {
        result->filtered = true;
        return true;
    }

    if (!fs_extract_id(source, opts, feature, feature_index, &id, &error_message, materialize_id)) {
        result->missing_id_skip = (opts->on_missing_id == FS_ON_MISSING_ID_SKIP);
        result->reason_value = error_message;
        return false;
    }

    if (mode == FS_MODE_READ_FEATURES) {
        properties = json_object_get(feature, "properties");
        if (!json_exists(properties) || json_type(properties) == JSON_NULL) {
            /* returned as "null" */
        } else if (json_type(properties) != JSON_OBJECT) {
            result->reason = "properties must be an object or null";
            return false;
        }
        result->properties = properties;
    }

    result->accepted = true;
    result->id = id;
    result->geometry = geometry;
    return true;
}

static void fs_parse_root(fs_source_t *source, struct json *features_out) {
    struct json_valid valid;
    struct json root;
    struct json type;
    struct json features;

    valid = json_validn_ex(source->data, source->len, 0);
    if (!valid.valid) {
        rb_raise(eTGGeometryParseError, "malformed JSON at byte %llu",
                 (unsigned long long)valid.pos);
    }

    root = json_parsen(source->data, source->len);
    if (!json_exists(root) || json_type(root) != JSON_OBJECT) {
        rb_raise(eTGGeometryParseError, "GeoJSON root must be a FeatureCollection object");
    }

    type = json_object_get(root, "type");
    if (!json_exists(type) || json_type(type) != JSON_STRING ||
        json_string_compare(type, "FeatureCollection") != 0) {
        rb_raise(eTGGeometryParseError, "GeoJSON root type must be FeatureCollection");
    }

    features = json_object_get(root, "features");
    if (!json_exists(features) || json_type(features) != JSON_ARRAY) {
        rb_raise(eTGGeometryParseError, "GeoJSON FeatureCollection features must be an Array");
    }

    *features_out = features;
}

static void fs_handle_invalid_or_raise(fs_source_t *source, fs_options_t *opts,
                                       fs_feature_result_t *feature_result, long feature_index,
                                       VALUE errors, long *skipped) {
    size_t offset =
        fs_json_offset(source, json_exists(feature_result->geometry) ? feature_result->geometry
                                                                     : feature_result->feature);
    VALUE reason =
        !NIL_P(feature_result->reason_value)
            ? feature_result->reason_value
            : rb_str_new_cstr(feature_result->reason ? feature_result->reason : "invalid feature");

    if (feature_result->missing_id_skip || opts->on_invalid == FS_ON_INVALID_SKIP) {
        (*skipped)++;
        fs_report_error(errors, opts->max_errors, feature_index, offset, reason);
        return;
    }

    fs_raise_parse_error_value(feature_index, offset, reason);
}

static VALUE fs_properties_json_string(struct json properties) {
    if (!json_exists(properties) || json_type(properties) == JSON_NULL) {
        return fs_cstr_utf8_string("null");
    }

    return fs_utf8_string(json_raw(properties), json_raw_length(properties));
}

/*
 * FeatureSource safe bulk executor.
 *
 * Heavy source traversal and TG geometry parsing run without GVL. The no-GVL phase stores only
 * C-owned data and json.c ranges backed by the owned source buffer. Ruby VALUE ids/strings/arrays
 * and rb_gc_adjust_memory_usage are only created/called again after the GVL is restored.
 */
typedef enum {
    FS_SAFE_ERROR_NONE,
    FS_SAFE_ERROR_NOMEM,
    FS_SAFE_ERROR_PARSE,
    FS_SAFE_ERROR_ARGUMENT,
    FS_SAFE_ERROR_SYSTEM,
} fs_safe_error_type_t;

typedef enum {
    FS_SAFE_ID_STRING,
    FS_SAFE_ID_INTEGER,
    FS_SAFE_ID_ORDINAL,
} fs_safe_id_kind_t;

typedef struct {
    fs_safe_id_kind_t kind;
    struct json json_value;
    long long integer_value;
    long feature_index;
} fs_safe_id_t;

typedef struct {
    fs_safe_id_t id;
    struct json geometry;
    struct json properties;
    bool properties_null;
} fs_safe_row_t;

typedef struct {
    fs_safe_id_t id;
    struct tg_geom *geom;
    struct tg_rect bbox;
    size_t geom_bytes;
    long ordinal;
} fs_safe_native_entry_t;

typedef struct {
    long feature_index;
    size_t byte_offset;
    char *reason;
} fs_safe_report_error_t;

typedef struct {
    fs_safe_error_type_t type;
    int sys_errno;
    long feature_index;
    size_t byte_offset;
    char *message;
} fs_safe_error_t;

typedef struct {
    char **keys;
    size_t *lens;
    long len;
    bool only_all;
    unsigned int only_mask;
    fs_on_invalid_t on_invalid;
    fs_on_missing_id_t on_missing_id;
    bool report;
    long max_errors;
    enum tg_index geometry_index;
    enum tg_geometry_index_strategy strategy;
    enum tg_geometry_index_predicate predicate;
} fs_safe_options_t;

typedef struct {
    fs_mode_t mode;
    fs_safe_options_t opts;

    char *path;
    char *source_data;
    size_t source_len;
    bool source_ruby_alloc;

    fs_safe_row_t *rows;
    long row_len;
    long row_cap;

    fs_safe_native_entry_t *entries;
    long entry_len;

    long skipped;
    long filtered;
    fs_safe_report_error_t *errors;
    long errors_len;
    long errors_cap;

    fs_safe_error_t fatal;
} fs_safe_job_t;

typedef struct {
    fs_safe_job_t *job;
    VALUE result;
} fs_safe_materialize_args_t;

static char *fs_safe_strdup_len(const char *ptr, size_t len) {
    char *copy;

    if (len > SIZE_MAX - 1)
        return NULL;
    copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    if (len > 0)
        memcpy(copy, ptr, len);
    copy[len] = '\0';
    return copy;
}

static char *fs_safe_strdup_cstr(const char *ptr) {
    return fs_safe_strdup_len(ptr ? ptr : "", strlen(ptr ? ptr : ""));
}

static char *fs_safe_format_feature_message(long feature_index, size_t byte_offset,
                                            const char *reason) {
    const char *r = reason ? reason : "invalid feature";
    int n = snprintf(NULL, 0, "feature %ld at byte %llu: %s", feature_index,
                     (unsigned long long)byte_offset, r);
    char *buf;

    if (n < 0)
        return NULL;
    buf = (char *)malloc((size_t)n + 1);
    if (!buf)
        return NULL;
    snprintf(buf, (size_t)n + 1, "feature %ld at byte %llu: %s", feature_index,
             (unsigned long long)byte_offset, r);
    return buf;
}

static void fs_safe_set_error(fs_safe_job_t *job, fs_safe_error_type_t type, const char *message) {
    if (job->fatal.type != FS_SAFE_ERROR_NONE)
        return;
    job->fatal.type = type;
    job->fatal.message = fs_safe_strdup_cstr(message);
    if (!job->fatal.message && type != FS_SAFE_ERROR_NOMEM) {
        job->fatal.type = FS_SAFE_ERROR_NOMEM;
    }
}

static void fs_safe_set_system_error(fs_safe_job_t *job, int err) {
    if (job->fatal.type != FS_SAFE_ERROR_NONE)
        return;
    job->fatal.type = FS_SAFE_ERROR_SYSTEM;
    job->fatal.sys_errno = err;
}

static void fs_safe_set_feature_error(fs_safe_job_t *job, fs_safe_error_type_t type,
                                      long feature_index, size_t byte_offset, const char *reason) {
    if (job->fatal.type != FS_SAFE_ERROR_NONE)
        return;
    job->fatal.type = type;
    job->fatal.feature_index = feature_index;
    job->fatal.byte_offset = byte_offset;
    job->fatal.message = fs_safe_format_feature_message(feature_index, byte_offset, reason);
    if (!job->fatal.message && type != FS_SAFE_ERROR_NOMEM) {
        job->fatal.type = FS_SAFE_ERROR_NOMEM;
    }
}

static bool fs_safe_add_report_error(fs_safe_job_t *job, long feature_index, size_t byte_offset,
                                     const char *reason) {
    fs_safe_report_error_t *grown;

    if (job->errors_len >= job->opts.max_errors)
        return true;

    if (job->errors_len == job->errors_cap) {
        long new_cap = job->errors_cap == 0 ? 8 : job->errors_cap * 2;
        if (new_cap < job->errors_cap || (size_t)new_cap > SIZE_MAX / sizeof(*job->errors)) {
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource report allocation overflow");
            return false;
        }
        grown =
            (fs_safe_report_error_t *)realloc(job->errors, (size_t)new_cap * sizeof(*job->errors));
        if (!grown) {
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource report allocation failed");
            return false;
        }
        job->errors = grown;
        job->errors_cap = new_cap;
    }

    job->errors[job->errors_len].feature_index = feature_index;
    job->errors[job->errors_len].byte_offset = byte_offset;
    job->errors[job->errors_len].reason = fs_safe_strdup_cstr(reason ? reason : "invalid feature");
    if (!job->errors[job->errors_len].reason) {
        fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM,
                          "FeatureSource report reason allocation failed");
        return false;
    }
    job->errors_len++;
    return true;
}

static size_t fs_safe_json_offset(fs_safe_job_t *job, struct json value) {
    const char *raw = json_raw(value);

    if (!raw || !job->source_data || raw < job->source_data ||
        raw > job->source_data + job->source_len)
        return 0;
    return (size_t)(raw - job->source_data);
}

static bool fs_safe_json_get_path(struct json root, fs_safe_options_t *opts, struct json *out) {
    struct json cur = root;

    for (long i = 0; i < opts->len; i++) {
        if (!json_exists(cur) || json_type(cur) != JSON_OBJECT)
            return false;
        cur = json_object_getn(cur, opts->keys[i], opts->lens[i]);
        if (!json_exists(cur))
            return false;
    }

    *out = cur;
    return true;
}

static unsigned int fs_safe_geometry_type_bit(struct json type_value) {
    if (!json_exists(type_value) || json_type(type_value) != JSON_STRING)
        return 0;
    if (json_string_compare(type_value, "Point") == 0)
        return FS_GEOM_POINT;
    if (json_string_compare(type_value, "LineString") == 0)
        return FS_GEOM_LINESTRING;
    if (json_string_compare(type_value, "Polygon") == 0)
        return FS_GEOM_POLYGON;
    if (json_string_compare(type_value, "MultiPoint") == 0)
        return FS_GEOM_MULTIPOINT;
    if (json_string_compare(type_value, "MultiLineString") == 0)
        return FS_GEOM_MULTILINESTRING;
    if (json_string_compare(type_value, "MultiPolygon") == 0)
        return FS_GEOM_MULTIPOLYGON;
    if (json_string_compare(type_value, "GeometryCollection") == 0)
        return FS_GEOM_GEOMETRYCOLLECTION;
    return 0;
}

static bool fs_safe_json_number_is_integer(struct json value) {
    const char *raw = json_raw(value);
    size_t len = json_raw_length(value);

    if (!raw || len == 0)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '.' || raw[i] == 'e' || raw[i] == 'E')
            return false;
    }
    return true;
}

static bool fs_safe_parse_integer_id(struct json value, long long *out, const char **reason) {
    const char *raw = json_raw(value);
    size_t len = json_raw_length(value);
    char stack[64];
    char *buf = stack;
    char *endp = NULL;
    long long parsed;
    bool ok;

    if (!fs_safe_json_number_is_integer(value)) {
        *reason = "invalid id: numeric id must be an integer";
        return false;
    }

    if (len >= sizeof(stack)) {
        buf = fs_safe_strdup_len(raw, len);
        if (!buf) {
            *reason = "id buffer allocation failed";
            return false;
        }
    } else {
        memcpy(buf, raw, len);
        buf[len] = '\0';
    }

    errno = 0;
    parsed = strtoll(buf, &endp, 10);
    ok = (errno == 0 && endp == buf + len);
    if (buf != stack)
        free(buf);

    if (!ok) {
        *reason = "invalid id: integer is out of range";
        return false;
    }

    *out = parsed;
    return true;
}

static bool fs_safe_extract_id(fs_safe_job_t *job, struct json feature, long feature_index,
                               fs_safe_id_t *id_out, const char **reason, bool *missing_id_skip,
                               bool *argument_error) {
    struct json id_json;

    *missing_id_skip = false;
    *argument_error = false;

    if (!fs_safe_json_get_path(feature, &job->opts, &id_json) || json_type(id_json) == JSON_NULL) {
        switch (job->opts.on_missing_id) {
        case FS_ON_MISSING_ID_ORDINAL:
            id_out->kind = FS_SAFE_ID_ORDINAL;
            id_out->feature_index = feature_index;
            return true;
        case FS_ON_MISSING_ID_SKIP:
            *reason = "missing id at configured path";
            *missing_id_skip = true;
            return false;
        case FS_ON_MISSING_ID_RAISE:
            *reason = "missing id at configured path";
            *argument_error = true;
            return false;
        }
    }

    switch (json_type(id_json)) {
    case JSON_STRING:
        id_out->kind = FS_SAFE_ID_STRING;
        id_out->json_value = id_json;
        id_out->feature_index = feature_index;
        return true;
    case JSON_NUMBER: {
        long long parsed = 0;
        if (!fs_safe_parse_integer_id(id_json, &parsed, reason)) {
            *argument_error = (job->opts.on_invalid == FS_ON_INVALID_RAISE);
            return false;
        }
        id_out->kind = FS_SAFE_ID_INTEGER;
        id_out->integer_value = parsed;
        id_out->feature_index = feature_index;
        return true;
    }
    default:
        *reason = "invalid id: expected JSON string or integer number";
        *argument_error = (job->opts.on_invalid == FS_ON_INVALID_RAISE);
        return false;
    }
}

typedef struct {
    bool accepted;
    bool filtered;
    bool missing_id_skip;
    bool argument_error;
    fs_safe_id_t id;
    struct json feature;
    struct json geometry;
    struct json properties;
    bool properties_null;
    const char *reason;
} fs_safe_feature_t;

static bool fs_safe_prepare_feature(fs_safe_job_t *job, struct json feature, long feature_index,
                                    fs_mode_t mode, fs_safe_feature_t *out) {
    struct json geometry;
    struct json geometry_type;
    struct json properties;
    unsigned int geom_bit;

    memset(out, 0, sizeof(*out));
    out->feature = feature;

    if (!json_exists(feature) || json_type(feature) != JSON_OBJECT) {
        out->reason = "feature is not an object";
        return false;
    }

    geometry = json_object_get(feature, "geometry");
    if (!json_exists(geometry) || json_type(geometry) == JSON_NULL) {
        out->reason = "missing geometry";
        return false;
    }
    if (json_type(geometry) != JSON_OBJECT) {
        out->reason = "geometry must be an object";
        return false;
    }

    geometry_type = json_object_get(geometry, "type");
    if (!json_exists(geometry_type) || json_type(geometry_type) != JSON_STRING) {
        out->reason = "geometry.type must be a string";
        return false;
    }

    geom_bit = fs_safe_geometry_type_bit(geometry_type);
    if (geom_bit == 0) {
        out->reason = "unsupported geometry.type";
        return false;
    }

    if (!job->opts.only_all && (job->opts.only_mask & geom_bit) == 0) {
        out->filtered = true;
        return true;
    }

    if (!fs_safe_extract_id(job, feature, feature_index, &out->id, &out->reason,
                            &out->missing_id_skip, &out->argument_error)) {
        return false;
    }

    if (mode == FS_MODE_READ_FEATURES) {
        properties = json_object_get(feature, "properties");
        if (!json_exists(properties) || json_type(properties) == JSON_NULL) {
            out->properties_null = true;
        } else if (json_type(properties) != JSON_OBJECT) {
            out->reason = "properties must be an object or null";
            return false;
        } else {
            out->properties = properties;
        }
    }

    out->accepted = true;
    out->geometry = geometry;
    return true;
}

static bool fs_safe_handle_invalid(fs_safe_job_t *job, fs_safe_feature_t *feature,
                                   long feature_index) {
    size_t offset = fs_safe_json_offset(job, json_exists(feature->geometry) ? feature->geometry
                                                                            : feature->feature);
    const char *reason = feature->reason ? feature->reason : "invalid feature";

    if (feature->missing_id_skip ||
        (!feature->argument_error && job->opts.on_invalid == FS_ON_INVALID_SKIP)) {
        job->skipped++;
        if (!fs_safe_add_report_error(job, feature_index, offset, reason))
            return false;
        return true;
    }

    fs_safe_set_feature_error(
        job, feature->argument_error ? FS_SAFE_ERROR_ARGUMENT : FS_SAFE_ERROR_PARSE, feature_index,
        offset, reason);
    return false;
}

static bool fs_safe_parse_root(fs_safe_job_t *job, struct json *features_out) {
    struct json_valid valid;
    struct json root;
    struct json type;
    struct json features;

    valid = json_validn_ex(job->source_data, job->source_len, 0);
    if (!valid.valid) {
        char msg[128];
        snprintf(msg, sizeof(msg), "malformed JSON at byte %llu", (unsigned long long)valid.pos);
        fs_safe_set_error(job, FS_SAFE_ERROR_PARSE, msg);
        return false;
    }

    root = json_parsen(job->source_data, job->source_len);
    if (!json_exists(root) || json_type(root) != JSON_OBJECT) {
        fs_safe_set_error(job, FS_SAFE_ERROR_PARSE,
                          "GeoJSON root must be a FeatureCollection object");
        return false;
    }

    type = json_object_get(root, "type");
    if (!json_exists(type) || json_type(type) != JSON_STRING ||
        json_string_compare(type, "FeatureCollection") != 0) {
        fs_safe_set_error(job, FS_SAFE_ERROR_PARSE, "GeoJSON root type must be FeatureCollection");
        return false;
    }

    features = json_object_get(root, "features");
    if (!json_exists(features) || json_type(features) != JSON_ARRAY) {
        fs_safe_set_error(job, FS_SAFE_ERROR_PARSE,
                          "GeoJSON FeatureCollection features must be an Array");
        return false;
    }

    *features_out = features;
    return true;
}

static char *fs_safe_tg_error_message(struct tg_geom *geom) {
    const char *err;
    char *msg;
    int n;

    if (!geom)
        return fs_safe_strdup_cstr("TG geometry allocation failed");
    err = tg_geom_error(geom);
    if (!err)
        return NULL;
    n = snprintf(NULL, 0, "invalid geometry: %s", err);
    if (n < 0)
        return NULL;
    msg = (char *)malloc((size_t)n + 1);
    if (!msg)
        return NULL;
    snprintf(msg, (size_t)n + 1, "invalid geometry: %s", err);
    return msg;
}

static bool fs_safe_validate_geometry(fs_safe_job_t *job, struct json geometry, long feature_index,
                                      bool *skipped_out) {
    *skipped_out = false;
    struct tg_geom *geom = tg_parse_geojsonn_ix(json_raw(geometry), json_raw_length(geometry),
                                                job->opts.geometry_index);
    char *message = fs_safe_tg_error_message(geom);

    if (!geom) {
        fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "TG geometry allocation failed");
        return false;
    }

    if (message) {
        if (job->opts.on_invalid == FS_ON_INVALID_SKIP) {
            job->skipped++;
            bool ok = fs_safe_add_report_error(job, feature_index,
                                               fs_safe_json_offset(job, geometry), message);
            free(message);
            tg_geom_free(geom);
            *skipped_out = true;
            return ok;
        }
        fs_safe_set_feature_error(job, FS_SAFE_ERROR_PARSE, feature_index,
                                  fs_safe_json_offset(job, geometry), message);
        free(message);
        tg_geom_free(geom);
        return false;
    }

    tg_geom_free(geom);
    return true;
}

static bool fs_safe_append_row(fs_safe_job_t *job, fs_safe_feature_t *feature) {
    fs_safe_row_t *grown;
    long new_cap;

    if (job->row_len == job->row_cap) {
        new_cap = job->row_cap == 0 ? 64 : job->row_cap * 2;
        if (new_cap < job->row_cap || (size_t)new_cap > SIZE_MAX / sizeof(*job->rows)) {
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource rows allocation overflow");
            return false;
        }
        grown = (fs_safe_row_t *)realloc(job->rows, (size_t)new_cap * sizeof(*job->rows));
        if (!grown) {
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource rows allocation failed");
            return false;
        }
        job->rows = grown;
        job->row_cap = new_cap;
    }

    memset(&job->rows[job->row_len], 0, sizeof(job->rows[job->row_len]));
    job->rows[job->row_len].id = feature->id;
    job->rows[job->row_len].geometry = feature->geometry;
    job->rows[job->row_len].properties = feature->properties;
    job->rows[job->row_len].properties_null = feature->properties_null;
    job->row_len++;
    return true;
}

static bool fs_safe_run_read(fs_safe_job_t *job, struct json features) {
    struct json feature;
    long feature_index = 0;

    for (feature = json_first(features); json_exists(feature);
         feature = json_next(feature), feature_index++) {
        fs_safe_feature_t prepared;

        if (!fs_safe_prepare_feature(job, feature, feature_index, job->mode, &prepared)) {
            if (!fs_safe_handle_invalid(job, &prepared, feature_index))
                return false;
            continue;
        }

        if (prepared.filtered) {
            job->filtered++;
            continue;
        }

        {
            bool geometry_skipped = false;
            if (!fs_safe_validate_geometry(job, prepared.geometry, feature_index,
                                           &geometry_skipped))
                return false;
            if (geometry_skipped)
                continue;
        }

        if (!fs_safe_append_row(job, &prepared))
            return false;
    }

    return true;
}

static bool fs_safe_count_build(fs_safe_job_t *job, struct json features, long *accepted_out) {
    struct json feature;
    long feature_index = 0;
    long accepted = 0;

    for (feature = json_first(features); json_exists(feature);
         feature = json_next(feature), feature_index++) {
        fs_safe_feature_t prepared;

        if (!fs_safe_prepare_feature(job, feature, feature_index, FS_MODE_BUILD_INDEX, &prepared)) {
            if (!fs_safe_handle_invalid(job, &prepared, feature_index))
                return false;
            continue;
        }

        if (prepared.filtered) {
            job->filtered++;
            continue;
        }
        if (prepared.accepted)
            accepted++;
    }

    *accepted_out = accepted;
    return true;
}

static bool fs_safe_fill_build(fs_safe_job_t *job, struct json features) {
    struct json feature;
    long feature_index = 0;
    long ordinal = 0;

    for (feature = json_first(features); json_exists(feature);
         feature = json_next(feature), feature_index++) {
        fs_safe_feature_t prepared;
        struct tg_geom *geom;
        char *message;

        if (!fs_safe_prepare_feature(job, feature, feature_index, FS_MODE_BUILD_INDEX, &prepared)) {
            if (!fs_safe_handle_invalid(job, &prepared, feature_index))
                return false;
            continue;
        }
        if (prepared.filtered || !prepared.accepted)
            continue;

        geom = tg_parse_geojsonn_ix(json_raw(prepared.geometry), json_raw_length(prepared.geometry),
                                    job->opts.geometry_index);
        message = fs_safe_tg_error_message(geom);
        if (!geom) {
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "TG geometry allocation failed");
            return false;
        }
        if (message) {
            fs_safe_set_feature_error(job, FS_SAFE_ERROR_PARSE, feature_index,
                                      fs_safe_json_offset(job, prepared.geometry), message);
            free(message);
            tg_geom_free(geom);
            return false;
        }

        job->entries[ordinal].id = prepared.id;
        job->entries[ordinal].geom = geom;
        job->entries[ordinal].bbox = tg_geom_rect(geom);
        job->entries[ordinal].geom_bytes = tg_geom_memsize(geom);
        job->entries[ordinal].ordinal = ordinal;
        ordinal++;
    }

    job->entry_len = ordinal;
    return true;
}

static bool fs_safe_read_file_no_gvl(fs_safe_job_t *job) {
    int fd;
    struct stat st;
    char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;

    fd = open(job->path, O_RDONLY);
    if (fd < 0) {
        fs_safe_set_system_error(job, errno);
        return false;
    }

    if (fstat(fd, &st) == 0 && st.st_size >= 0) {
        cap = (size_t)st.st_size;
        if (cap > SIZE_MAX - 1) {
            close(fd);
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource file is too large");
            return false;
        }
        buf = (char *)malloc(cap + 1);
        if (!buf) {
            close(fd);
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource file allocation failed");
            return false;
        }
    } else {
        cap = 8192;
        buf = (char *)malloc(cap + 1);
        if (!buf) {
            close(fd);
            fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource file allocation failed");
            return false;
        }
    }

    for (;;) {
        ssize_t n;
        if (len == cap) {
            size_t new_cap = cap < 8192 ? 8192 : cap * 2;
            char *grown;
            if (new_cap < cap || new_cap > SIZE_MAX - 1) {
                free(buf);
                close(fd);
                fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM,
                                  "FeatureSource file allocation overflow");
                return false;
            }
            grown = (char *)realloc(buf, new_cap + 1);
            if (!grown) {
                free(buf);
                close(fd);
                fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM, "FeatureSource file allocation failed");
                return false;
            }
            buf = grown;
            cap = new_cap;
        }

        n = read(fd, buf + len, cap - len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            int err = errno;
            free(buf);
            close(fd);
            fs_safe_set_system_error(job, err);
            return false;
        }
        if (n == 0)
            break;
        len += (size_t)n;
    }

    if (close(fd) != 0) {
        int err = errno;
        free(buf);
        fs_safe_set_system_error(job, err);
        return false;
    }

    buf[len] = '\0';
    job->source_data = buf;
    job->source_len = len;
    job->source_ruby_alloc = false;
    return true;
}

static void *fs_safe_run_no_gvl(void *ptr) {
    fs_safe_job_t *job = (fs_safe_job_t *)ptr;
    struct json features;

    if (job->path && !job->source_data) {
        if (!fs_safe_read_file_no_gvl(job))
            return NULL;
    }

    if (!fs_safe_parse_root(job, &features))
        return NULL;

    if (job->mode == FS_MODE_BUILD_INDEX) {
        long accepted = 0;
        if (!fs_safe_count_build(job, features, &accepted))
            return NULL;
        if (accepted > 0) {
            if ((size_t)accepted > SIZE_MAX / sizeof(*job->entries)) {
                fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM,
                                  "FeatureSource entries allocation overflow");
                return NULL;
            }
            job->entries =
                (fs_safe_native_entry_t *)calloc((size_t)accepted, sizeof(*job->entries));
            if (!job->entries) {
                fs_safe_set_error(job, FS_SAFE_ERROR_NOMEM,
                                  "FeatureSource entries allocation failed");
                return NULL;
            }
        }
        if (!fs_safe_fill_build(job, features))
            return NULL;
    } else {
        if (!fs_safe_run_read(job, features))
            return NULL;
    }

    return NULL;
}

static void fs_safe_run_without_gvl(fs_safe_job_t *job) {
#if defined(HAVE_RB_NOGVL_OFFLOAD_SAFE)
    rb_nogvl(fs_safe_run_no_gvl, job, RUBY_UBF_IO, NULL, RB_NOGVL_OFFLOAD_SAFE);
#else
    rb_thread_call_without_gvl(fs_safe_run_no_gvl, job, RUBY_UBF_IO, NULL);
#endif
}

static void fs_safe_free_c_options(fs_safe_options_t *opts) {
    if (!opts)
        return;
    if (opts->keys) {
        for (long i = 0; i < opts->len; i++) {
            free(opts->keys[i]);
        }
        free(opts->keys);
        opts->keys = NULL;
    }
    free(opts->lens);
    opts->lens = NULL;
    opts->len = 0;
}

static void fs_safe_cleanup(fs_safe_job_t *job) {
    if (!job)
        return;

    if (job->entries) {
        for (long i = 0; i < job->entry_len; i++) {
            if (job->entries[i].geom) {
                tg_geom_free(job->entries[i].geom);
                job->entries[i].geom = NULL;
            }
        }
        free(job->entries);
        job->entries = NULL;
        job->entry_len = 0;
    }

    free(job->rows);
    job->rows = NULL;
    job->row_len = 0;
    job->row_cap = 0;

    if (job->errors) {
        for (long i = 0; i < job->errors_len; i++) {
            free(job->errors[i].reason);
        }
        free(job->errors);
        job->errors = NULL;
        job->errors_len = 0;
        job->errors_cap = 0;
    }

    if (job->source_data) {
        if (job->source_ruby_alloc)
            ruby_xfree(job->source_data);
        else
            free(job->source_data);
        job->source_data = NULL;
        job->source_len = 0;
    }

    free(job->path);
    job->path = NULL;
    free(job->fatal.message);
    job->fatal.message = NULL;
    fs_safe_free_c_options(&job->opts);
}

static VALUE fs_safe_job_ensure(VALUE arg) {
    fs_safe_job_t *job = (fs_safe_job_t *)arg;
    fs_safe_cleanup(job);
    return Qnil;
}

static VALUE fs_safe_id_to_value(fs_safe_id_t *id) {
    switch (id->kind) {
    case FS_SAFE_ID_STRING:
        return fs_copy_json_string_value(id->json_value);
    case FS_SAFE_ID_INTEGER:
        return LL2NUM(id->integer_value);
    case FS_SAFE_ID_ORDINAL:
        return fs_missing_id_ordinal(id->feature_index);
    }
    return Qnil;
}

static VALUE fs_safe_properties_to_value(fs_safe_row_t *row) {
    if (row->properties_null || !json_exists(row->properties))
        return fs_cstr_utf8_string("null");
    return fs_utf8_string(json_raw(row->properties), json_raw_length(row->properties));
}

static void fs_safe_raise_fatal(fs_safe_job_t *job) {
    switch (job->fatal.type) {
    case FS_SAFE_ERROR_NONE:
        return;
    case FS_SAFE_ERROR_NOMEM:
        rb_raise(rb_eNoMemError, "%s",
                 job->fatal.message ? job->fatal.message : "FeatureSource allocation failed");
    case FS_SAFE_ERROR_PARSE:
        rb_raise(eTGGeometryParseError, "%s",
                 job->fatal.message ? job->fatal.message : "FeatureSource parse error");
    case FS_SAFE_ERROR_ARGUMENT:
        rb_raise(eTGGeometryArgumentError, "%s",
                 job->fatal.message ? job->fatal.message : "FeatureSource argument error");
    case FS_SAFE_ERROR_SYSTEM:
        rb_syserr_fail(job->fatal.sys_errno, job->path ? job->path : "FeatureSource file read");
    }
}

static VALUE fs_safe_materialize_rows(fs_safe_job_t *job) {
    VALUE rows = rb_ary_new_capa(job->row_len);

    for (long i = 0; i < job->row_len; i++) {
        VALUE id = fs_safe_id_to_value(&job->rows[i].id);
        VALUE geom_json =
            fs_utf8_string(json_raw(job->rows[i].geometry), json_raw_length(job->rows[i].geometry));
        if (job->mode == FS_MODE_READ_ENTRIES) {
            rb_ary_push(rows, rb_ary_new_from_args(2, id, geom_json));
        } else {
            VALUE props_json = fs_safe_properties_to_value(&job->rows[i]);
            rb_ary_push(rows, rb_ary_new_from_args(3, id, geom_json, props_json));
        }
    }

    if (job->opts.report) {
        VALUE report = rb_hash_new();
        VALUE errors = rb_ary_new_capa(job->errors_len);
        for (long i = 0; i < job->errors_len; i++) {
            VALUE reason = fs_cstr_utf8_string(job->errors[i].reason);
            rb_ary_push(errors, fs_error_hash(job->errors[i].feature_index,
                                              job->errors[i].byte_offset, reason));
        }
        rb_hash_aset(report,
                     job->mode == FS_MODE_READ_ENTRIES ? fs_sym("entries") : fs_sym("features"),
                     rows);
        rb_hash_aset(report, fs_sym("skipped"), LONG2NUM(job->skipped));
        rb_hash_aset(report, fs_sym("filtered"), LONG2NUM(job->filtered));
        rb_hash_aset(report, fs_sym("errors"), errors);
        RB_GC_GUARD(rows);
        RB_GC_GUARD(errors);
        RB_GC_GUARD(report);
        return report;
    }

    RB_GC_GUARD(rows);
    return rows;
}

typedef struct {
    fs_safe_job_t *job;
    tg_index_t *idx;
} fs_safe_index_finalize_args_t;

static VALUE fs_safe_index_finalize_body(VALUE arg) {
    fs_safe_index_finalize_args_t *a = (fs_safe_index_finalize_args_t *)arg;
    fs_safe_job_t *job = a->job;
    tg_index_t *idx = a->idx;

    if (job->entry_len > 0) {
        if ((size_t)job->entry_len > SIZE_MAX / sizeof(tg_index_entry_t)) {
            rb_raise(rb_eNoMemError, "entries allocation size overflow");
        }
        idx->entries = calloc((size_t)job->entry_len, sizeof(tg_index_entry_t));
        if (!idx->entries) {
            rb_raise(rb_eNoMemError, "entries allocation failed");
        }
        idx->entries_bytes = (size_t)job->entry_len * sizeof(tg_index_entry_t);
        rb_gc_adjust_memory_usage((ssize_t)idx->entries_bytes);
    }

    for (long i = 0; i < job->entry_len; i++) {
        tg_index_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        entry.id = fs_safe_id_to_value(&job->entries[i].id);
        entry.geom_owner = Qnil;
        entry.geom = job->entries[i].geom;
        entry.bbox = job->entries[i].bbox;
        entry.geom_bytes = job->entries[i].geom_bytes;
        entry.ordinal = i;
        entry.owned = true;

        job->entries[i].geom = NULL; /* ownership moves to idx */
        idx->entries[i] = entry;
        idx->initialized++;
        idx->owned_geom_bytes_total += entry.geom_bytes;
        if (entry.geom_bytes > 0)
            rb_gc_adjust_memory_usage((ssize_t)entry.geom_bytes);
        index_expand_bbox(idx, entry.bbox);
    }

    if (idx->strategy == TG_GEOMETRY_INDEX_STRATEGY_RTREE) {
        index_build_rtree(idx);
    }

    return Qnil;
}

static VALUE fs_safe_materialize_index(fs_safe_job_t *job) {
    tg_index_t *idx;
    VALUE wrapper;
    fs_safe_index_finalize_args_t args;
    int state = 0;

    wrapper = TypedData_Make_Struct(cTGGeometryIndex, tg_index_t, &tg_index_type, idx);
    idx->len = job->entry_len;
    idx->capacity = job->entry_len;
    idx->initialized = 0;
    idx->strategy = job->opts.strategy;
    idx->predicate = job->opts.predicate;
    idx->rtree = NULL;
    idx->frozen = false;
    idx->has_bbox = false;

    args.job = job;
    args.idx = idx;
    rb_protect(fs_safe_index_finalize_body, (VALUE)&args, &state);
    if (state) {
        index_dispose(idx);
        RB_GC_GUARD(wrapper);
        rb_jump_tag(state);
    }

    idx->frozen = true;
    rb_obj_freeze(wrapper);
    RB_GC_GUARD(wrapper);
    return wrapper;
}

static VALUE fs_safe_materialize(VALUE arg) {
    fs_safe_materialize_args_t *a = (fs_safe_materialize_args_t *)arg;
    fs_safe_job_t *job = a->job;

    fs_safe_raise_fatal(job);
    if (job->mode == FS_MODE_BUILD_INDEX)
        a->result = fs_safe_materialize_index(job);
    else
        a->result = fs_safe_materialize_rows(job);
    return a->result;
}

static void fs_safe_copy_options(fs_safe_job_t *job, fs_options_t *opts) {
    long len = RARRAY_LEN(opts->id_path);

    memset(&job->opts, 0, sizeof(job->opts));
    job->opts.only_all = opts->only_all;
    job->opts.only_mask = opts->only_mask;
    job->opts.on_invalid = opts->on_invalid;
    job->opts.on_missing_id = opts->on_missing_id;
    job->opts.report = opts->report;
    job->opts.max_errors = opts->max_errors;
    job->opts.geometry_index = opts->geometry_index;
    job->opts.strategy = opts->strategy;
    job->opts.predicate = opts->predicate;
    job->opts.len = len;

    if (len > 0) {
        job->opts.keys = calloc((size_t)len, sizeof(char *));
        job->opts.lens = calloc((size_t)len, sizeof(size_t));
        if (!job->opts.keys || !job->opts.lens) {
            fs_safe_free_c_options(&job->opts);
            rb_raise(rb_eNoMemError, "FeatureSource id path allocation failed");
        }
        for (long i = 0; i < len; i++) {
            VALUE key = rb_ary_entry(opts->id_path, i);
            StringValue(key);
            job->opts.lens[i] = (size_t)RSTRING_LEN(key);
            job->opts.keys[i] = fs_safe_strdup_len(RSTRING_PTR(key), job->opts.lens[i]);
            if (!job->opts.keys[i]) {
                fs_safe_free_c_options(&job->opts);
                rb_raise(rb_eNoMemError, "FeatureSource id path key allocation failed");
            }
        }
    }
}

typedef struct {
    fs_safe_job_t *job;
    fs_safe_materialize_args_t materialize;
} fs_safe_dispatch_ctx_t;

static VALUE fs_safe_dispatch_body(VALUE arg) {
    fs_safe_dispatch_ctx_t *ctx = (fs_safe_dispatch_ctx_t *)arg;

    fs_safe_run_without_gvl(ctx->job);
    ctx->materialize.job = ctx->job;
    return fs_safe_materialize((VALUE)&ctx->materialize);
}

static VALUE fs_safe_dispatch_json(VALUE json_string, VALUE kwargs, fs_mode_t mode) {
    fs_args_t fs;
    fs_safe_job_t job;
    fs_safe_dispatch_ctx_t ctx;

    memset(&fs, 0, sizeof(fs));
    memset(&job, 0, sizeof(job));
    memset(&ctx, 0, sizeof(ctx));

    fs.mode = mode;
    fs.opts = fs_parse_options(kwargs, mode);
    job.mode = mode;
    {
        VALUE str = json_string;
        StringValue(str);
        if ((size_t)RSTRING_LEN(str) > SIZE_MAX - 1)
            rb_raise(rb_eNoMemError, "FeatureSource input is too large");

        fs_safe_copy_options(&job, &fs.opts);
        job.source_len = (size_t)RSTRING_LEN(str);
        job.source_data = (char *)malloc(job.source_len + 1);
        if (!job.source_data) {
            fs_safe_job_ensure((VALUE)&job);
            rb_raise(rb_eNoMemError, "FeatureSource input allocation failed");
        }
        memcpy(job.source_data, RSTRING_PTR(str), job.source_len);
        job.source_data[job.source_len] = '\0';
        job.source_ruby_alloc = false;
        RB_GC_GUARD(str);
    }

    ctx.job = &job;
    return rb_ensure(fs_safe_dispatch_body, (VALUE)&ctx, fs_safe_job_ensure, (VALUE)&job);
}

static VALUE fs_safe_dispatch_file(VALUE path, VALUE kwargs, fs_mode_t mode) {
    fs_args_t fs;
    fs_safe_job_t job;
    fs_safe_dispatch_ctx_t ctx;
    VALUE path_str = path;

    memset(&fs, 0, sizeof(fs));
    memset(&job, 0, sizeof(job));
    memset(&ctx, 0, sizeof(ctx));

    StringValueCStr(path_str);
    fs.mode = mode;
    fs.opts = fs_parse_options(kwargs, mode);
    job.mode = mode;
    fs_safe_copy_options(&job, &fs.opts);
    job.path = fs_safe_strdup_cstr(StringValueCStr(path_str));
    if (!job.path) {
        fs_safe_job_ensure((VALUE)&job);
        rb_raise(rb_eNoMemError, "FeatureSource path allocation failed");
    }

    ctx.job = &job;
    return rb_ensure(fs_safe_dispatch_body, (VALUE)&ctx, fs_safe_job_ensure, (VALUE)&job);
}

static VALUE fs_read_body(VALUE arg) {
    fs_args_t *fs = (fs_args_t *)arg;
    struct json features;
    struct json feature;
    VALUE rows;
    VALUE errors;
    long feature_index = 0;
    long skipped = 0;
    long filtered = 0;

    fs_parse_root(&fs->source, &features);
    rows = rb_ary_new();
    errors = rb_ary_new();

    for (feature = json_first(features); json_exists(feature);
         feature = json_next(feature), feature_index++) {
        fs_feature_result_t prepared;
        VALUE tg_error = Qnil;

        if (!fs_feature_prepare(&fs->source, &fs->opts, fs->mode, feature, feature_index, true,
                                &prepared)) {
            fs_handle_invalid_or_raise(&fs->source, &fs->opts, &prepared, feature_index, errors,
                                       &skipped);
            continue;
        }

        if (prepared.filtered) {
            filtered++;
            continue;
        }

        if (!fs_validate_geometry_or_error(prepared.geometry, fs->opts.geometry_index, &tg_error)) {
            prepared.reason_value = rb_str_plus(rb_str_new_cstr("invalid geometry: "), tg_error);
            fs_handle_invalid_or_raise(&fs->source, &fs->opts, &prepared, feature_index, errors,
                                       &skipped);
            continue;
        }

        if (fs->mode == FS_MODE_READ_ENTRIES) {
            VALUE geom_json =
                fs_utf8_string(json_raw(prepared.geometry), json_raw_length(prepared.geometry));
            rb_ary_push(rows, rb_ary_new_from_args(2, prepared.id, geom_json));
        } else {
            VALUE geom_json =
                fs_utf8_string(json_raw(prepared.geometry), json_raw_length(prepared.geometry));
            VALUE props_json = fs_properties_json_string(prepared.properties);
            rb_ary_push(rows, rb_ary_new_from_args(3, prepared.id, geom_json, props_json));
        }
    }

    if (fs->opts.report) {
        VALUE report = rb_hash_new();
        rb_hash_aset(report,
                     fs->mode == FS_MODE_READ_ENTRIES ? fs_sym("entries") : fs_sym("features"),
                     rows);
        rb_hash_aset(report, fs_sym("skipped"), LONG2NUM(skipped));
        rb_hash_aset(report, fs_sym("filtered"), LONG2NUM(filtered));
        rb_hash_aset(report, fs_sym("errors"), errors);
        RB_GC_GUARD(rows);
        RB_GC_GUARD(errors);
        RB_GC_GUARD(report);
        RB_GC_GUARD(fs->opts.id_path);
        return report;
    }

    RB_GC_GUARD(errors);
    RB_GC_GUARD(rows);
    RB_GC_GUARD(fs->opts.id_path);
    return rows;
}

static long fs_count_accepted_features(fs_args_t *fs, struct json features) {
    struct json feature;
    long feature_index = 0;
    long accepted = 0;

    for (feature = json_first(features); json_exists(feature);
         feature = json_next(feature), feature_index++) {
        fs_feature_result_t prepared;
        long skipped = 0;

        if (!fs_feature_prepare(&fs->source, &fs->opts, FS_MODE_BUILD_INDEX, feature, feature_index,
                                false, &prepared)) {
            fs_handle_invalid_or_raise(&fs->source, &fs->opts, &prepared, feature_index, Qnil,
                                       &skipped);
        }

        if (prepared.filtered)
            continue;
        if (prepared.accepted)
            accepted++;
    }

    return accepted;
}

static void fs_fill_index_entry_from_geometry(tg_index_t *idx, long ordinal, VALUE id,
                                              struct json geometry, enum tg_index geometry_index,
                                              fs_source_t *source, long feature_index) {
    tg_index_entry_t entry;
    struct tg_geom *geom;
    const char *err;

    geom = tg_parse_geojsonn_ix(json_raw(geometry), json_raw_length(geometry), geometry_index);
    if (!geom) {
        rb_raise(rb_eNoMemError, "TG geometry allocation failed");
    }

    err = tg_geom_error(geom);
    if (err) {
        VALUE msg = rb_str_new_cstr(err);
        VALUE full;
        tg_geom_free(geom);
        full = rb_str_plus(rb_str_new_cstr("invalid geometry: "), msg);
        fs_raise_parse_error_value(feature_index, fs_json_offset(source, geometry), full);
    }

    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    entry.geom_owner = Qnil;
    entry.geom = geom;
    entry.bbox = tg_geom_rect(geom);
    entry.geom_bytes = tg_geom_memsize(geom);
    entry.ordinal = ordinal;
    entry.owned = true;

    idx->entries[ordinal] = entry;
    idx->initialized++;
    idx->owned_geom_bytes_total += entry.geom_bytes;
    if (entry.geom_bytes > 0) {
        rb_gc_adjust_memory_usage((ssize_t)entry.geom_bytes);
    }
    index_expand_bbox(idx, entry.bbox);
}

static VALUE fs_build_index_body(VALUE arg) {
    fs_build_args_t *build = (fs_build_args_t *)arg;
    fs_args_t *fs = build->fs;
    tg_index_t *idx = build->idx;
    struct json feature;
    long feature_index = 0;
    long ordinal = 0;

    for (feature = json_first(build->features); json_exists(feature);
         feature = json_next(feature), feature_index++) {
        fs_feature_result_t prepared;
        long skipped = 0;

        if (!fs_feature_prepare(&fs->source, &fs->opts, FS_MODE_BUILD_INDEX, feature, feature_index,
                                true, &prepared)) {
            fs_handle_invalid_or_raise(&fs->source, &fs->opts, &prepared, feature_index, Qnil,
                                       &skipped);
        }

        if (prepared.filtered)
            continue;
        if (!prepared.accepted)
            continue;

        fs_fill_index_entry_from_geometry(idx, ordinal, prepared.id, prepared.geometry,
                                          fs->opts.geometry_index, &fs->source, feature_index);
        ordinal++;
    }

    if (idx->initialized != idx->len) {
        rb_raise(eTGGeometryError, "internal FeatureSource index initialization mismatch");
    }

    if (idx->strategy == TG_GEOMETRY_INDEX_STRATEGY_RTREE) {
        index_build_rtree(idx);
    }

    return Qnil;
}

static VALUE fs_build_index(VALUE arg) {
    fs_args_t *fs = (fs_args_t *)arg;
    struct json features;
    long accepted;
    tg_index_t *idx;
    VALUE wrapper;
    fs_build_args_t build;
    int state = 0;

    fs_parse_root(&fs->source, &features);
    accepted = fs_count_accepted_features(fs, features);

    wrapper = TypedData_Make_Struct(cTGGeometryIndex, tg_index_t, &tg_index_type, idx);
    idx->len = accepted;
    idx->capacity = accepted;
    idx->initialized = 0;
    idx->strategy = fs->opts.strategy;
    idx->predicate = fs->opts.predicate;
    idx->rtree = NULL;
    idx->frozen = false;
    idx->has_bbox = false;

    if (accepted > 0) {
        if ((size_t)accepted > SIZE_MAX / sizeof(tg_index_entry_t)) {
            rb_raise(rb_eNoMemError, "entries allocation size overflow");
        }
        idx->entries = calloc((size_t)accepted, sizeof(tg_index_entry_t));
        if (!idx->entries) {
            rb_raise(rb_eNoMemError, "entries allocation failed");
        }
        idx->entries_bytes = (size_t)accepted * sizeof(tg_index_entry_t);
        rb_gc_adjust_memory_usage((ssize_t)idx->entries_bytes);
    }

    build.fs = fs;
    build.features = features;
    build.wrapper = wrapper;
    build.idx = idx;

    rb_protect(fs_build_index_body, (VALUE)&build, &state);
    if (state) {
        index_dispose(idx);
        RB_GC_GUARD(wrapper);
        RB_GC_GUARD(fs->opts.id_path);
        rb_jump_tag(state);
    }

    idx->frozen = true;
    rb_obj_freeze(wrapper);

    RB_GC_GUARD(wrapper);
    RB_GC_GUARD(fs->opts.id_path);
    return wrapper;
}

static VALUE fs_source_ensure(VALUE arg) {
    fs_args_t *fs = (fs_args_t *)arg;

    if (fs->source.data) {
        ruby_xfree(fs->source.data);
        fs->source.data = NULL;
        fs->source.len = 0;
    }

    return Qnil;
}

static void fs_copy_source_from_string(fs_args_t *fs, VALUE source) {
    VALUE str = source;
    StringValue(str);

    if ((size_t)RSTRING_LEN(str) > SIZE_MAX - 1) {
        rb_raise(rb_eNoMemError, "FeatureSource input is too large");
    }

    fs->source.len = (size_t)RSTRING_LEN(str);
    fs->source.data = ruby_xmalloc(fs->source.len + 1);
    memcpy(fs->source.data, RSTRING_PTR(str), fs->source.len);
    fs->source.data[fs->source.len] = '\0';
    RB_GC_GUARD(str);
}

static VALUE __attribute__((unused)) fs_dispatch(VALUE source_string, VALUE kwargs,
                                                 fs_mode_t mode) {
    fs_args_t fs;

    memset(&fs, 0, sizeof(fs));
    fs.mode = mode;
    fs.opts = fs_parse_options(kwargs, mode);
    fs_copy_source_from_string(&fs, source_string);

    if (mode == FS_MODE_BUILD_INDEX) {
        return rb_ensure(fs_build_index, (VALUE)&fs, fs_source_ensure, (VALUE)&fs);
    }

    return rb_ensure(fs_read_body, (VALUE)&fs, fs_source_ensure, (VALUE)&fs);
}

static VALUE __attribute__((unused)) fs_read_file_to_string(VALUE path) {
    return rb_funcall(rb_cFile, rb_intern("binread"), 1, path);
}

static VALUE fs_read_io_to_string(VALUE io) {
    VALUE str;

    if (!rb_respond_to(io, id_read)) {
        rb_raise(rb_eTypeError, "io must respond to read");
    }
    str = rb_funcall(io, id_read, 0);
    if (!RB_TYPE_P(str, T_STRING)) {
        rb_raise(rb_eTypeError, "io.read must return String");
    }
    return str;
}

static VALUE rb_tg_feature_source_read_entries_json(int argc, VALUE *argv, VALUE self) {
    VALUE json_string;
    VALUE kwargs;
    (void)self;
    rb_scan_args(argc, argv, "1:", &json_string, &kwargs);
    return fs_safe_dispatch_json(json_string, kwargs, FS_MODE_READ_ENTRIES);
}

static VALUE rb_tg_feature_source_read_features_json(int argc, VALUE *argv, VALUE self) {
    VALUE json_string;
    VALUE kwargs;
    (void)self;
    rb_scan_args(argc, argv, "1:", &json_string, &kwargs);
    return fs_safe_dispatch_json(json_string, kwargs, FS_MODE_READ_FEATURES);
}

static VALUE rb_tg_feature_source_build_index_json(int argc, VALUE *argv, VALUE self) {
    VALUE json_string;
    VALUE kwargs;
    (void)self;
    rb_scan_args(argc, argv, "1:", &json_string, &kwargs);
    return fs_safe_dispatch_json(json_string, kwargs, FS_MODE_BUILD_INDEX);
}

static VALUE rb_tg_feature_source_read_entries_file(int argc, VALUE *argv, VALUE self) {
    VALUE path;
    VALUE kwargs;
    (void)self;
    rb_scan_args(argc, argv, "1:", &path, &kwargs);
    return fs_safe_dispatch_file(path, kwargs, FS_MODE_READ_ENTRIES);
}

static VALUE rb_tg_feature_source_read_features_file(int argc, VALUE *argv, VALUE self) {
    VALUE path;
    VALUE kwargs;
    (void)self;
    rb_scan_args(argc, argv, "1:", &path, &kwargs);
    return fs_safe_dispatch_file(path, kwargs, FS_MODE_READ_FEATURES);
}

static VALUE rb_tg_feature_source_build_index_file(int argc, VALUE *argv, VALUE self) {
    VALUE path;
    VALUE kwargs;
    (void)self;
    rb_scan_args(argc, argv, "1:", &path, &kwargs);
    return fs_safe_dispatch_file(path, kwargs, FS_MODE_BUILD_INDEX);
}

static VALUE rb_tg_feature_source_read_entries_io(int argc, VALUE *argv, VALUE self) {
    VALUE io;
    VALUE kwargs;
    VALUE json_string;
    (void)self;
    rb_scan_args(argc, argv, "1:", &io, &kwargs);
    json_string = fs_read_io_to_string(io);
    return fs_safe_dispatch_json(json_string, kwargs, FS_MODE_READ_ENTRIES);
}

static VALUE rb_tg_feature_source_read_features_io(int argc, VALUE *argv, VALUE self) {
    VALUE io;
    VALUE kwargs;
    VALUE json_string;
    (void)self;
    rb_scan_args(argc, argv, "1:", &io, &kwargs);
    json_string = fs_read_io_to_string(io);
    return fs_safe_dispatch_json(json_string, kwargs, FS_MODE_READ_FEATURES);
}

static VALUE rb_tg_feature_source_build_index_io(int argc, VALUE *argv, VALUE self) {
    VALUE io;
    VALUE kwargs;
    VALUE json_string;
    (void)self;
    rb_scan_args(argc, argv, "1:", &io, &kwargs);
    json_string = fs_read_io_to_string(io);
    return fs_safe_dispatch_json(json_string, kwargs, FS_MODE_BUILD_INDEX);
}

RUBY_FUNC_EXPORTED void Init_tg_geometry_ext_geometry_ext(void) {
    tg_geometry_vendor_header_sanity();

    id_format = rb_intern("format");
    id_index = rb_intern("index");
    id_srid = rb_intern("srid");
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
    id_id = rb_intern("id");
    id_only = rb_intern("only");
    id_on_invalid = rb_intern("on_invalid");
    id_on_missing_id = rb_intern("on_missing_id");
    id_report = rb_intern("report");
    id_max_errors = rb_intern("max_errors");
    id_read = rb_intern("read");
    id_raise = rb_intern("raise");
    id_skip = rb_intern("skip");
    id_ordinal = rb_intern("ordinal");
    id_polygon = rb_intern("polygon");
    id_multipolygon = rb_intern("multipolygon");
    id_point = rb_intern("point");
    id_linestring = rb_intern("linestring");
    id_multipoint = rb_intern("multipoint");
    id_multilinestring = rb_intern("multilinestring");
    id_geometrycollection = rb_intern("geometrycollection");
    id_exterior = rb_intern("exterior");
    id_holes = rb_intern("holes");

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
    rb_define_singleton_method(mTGGeometry, "line_string", rb_tg_geometry_line_string, -1);
    rb_define_singleton_method(mTGGeometry, "polygon", rb_tg_geometry_polygon, -1);
    rb_define_singleton_method(mTGGeometry, "multi_polygon", rb_tg_geometry_multi_polygon, -1);

    cTGGeometryGeom = rb_define_class_under(mTGGeometry, "Geom", rb_cObject);
    rb_undef_alloc_func(cTGGeometryGeom);
    rb_define_method(cTGGeometryGeom, "type", rb_tg_geometry_geom_type, 0);
    rb_define_method(cTGGeometryGeom, "srid", rb_tg_geometry_geom_srid, 0);
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
    rb_define_method(cTGGeometryGeom, "to_ewkb", rb_tg_geometry_geom_to_ewkb, -1);
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
    rb_define_method(cTGGeometryLine, "nearest_segment", rb_tg_geometry_line_nearest_segment, 2);

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
    rb_define_method(cTGGeometryRing, "nearest_segment", rb_tg_geometry_ring_nearest_segment, 2);

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

    cTGGeometryNearestSegment = rb_define_class_under(mTGGeometry, "NearestSegment", rb_cObject);
    rb_undef_alloc_func(cTGGeometryNearestSegment);
    rb_define_method(cTGGeometryNearestSegment, "segment", rb_tg_geometry_nearest_segment_segment,
                     0);
    rb_define_method(cTGGeometryNearestSegment, "index", rb_tg_geometry_nearest_segment_index, 0);
    rb_define_method(cTGGeometryNearestSegment, "distance", rb_tg_geometry_nearest_segment_distance,
                     0);
    rb_define_method(cTGGeometryNearestSegment, "point", rb_tg_geometry_nearest_segment_point, 0);

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

    mTGGeometryFeatureSource = rb_define_module_under(mTGGeometry, "FeatureSource");
    rb_define_singleton_method(mTGGeometryFeatureSource, "read_entries_file",
                               rb_tg_feature_source_read_entries_file, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "read_entries_json",
                               rb_tg_feature_source_read_entries_json, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "read_entries_io",
                               rb_tg_feature_source_read_entries_io, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "read_features_file",
                               rb_tg_feature_source_read_features_file, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "read_features_json",
                               rb_tg_feature_source_read_features_json, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "read_features_io",
                               rb_tg_feature_source_read_features_io, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "build_index_file",
                               rb_tg_feature_source_build_index_file, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "build_index_json",
                               rb_tg_feature_source_build_index_json, -1);
    rb_define_singleton_method(mTGGeometryFeatureSource, "build_index_io",
                               rb_tg_feature_source_build_index_io, -1);

    cTGGeometryIndex = rb_define_class_under(mTGGeometry, "Index", rb_cObject);
    rb_undef_alloc_func(cTGGeometryIndex);
    rb_define_singleton_method(cTGGeometryIndex, "build", rb_tg_geometry_index_build, -1);
    rb_define_method(cTGGeometryIndex, "find_covering", rb_tg_geometry_index_find_covering, 2);
    rb_define_method(cTGGeometryIndex, "covering_ids", rb_tg_geometry_index_covering_ids, 2);
    rb_define_method(cTGGeometryIndex, "intersecting_geom_ids",
                     rb_tg_geometry_index_intersecting_geom_ids, 1);
    rb_define_method(cTGGeometryIndex, "covering_geom_ids", rb_tg_geometry_index_covering_geom_ids,
                     1);
    rb_define_method(cTGGeometryIndex, "containing_geom_ids",
                     rb_tg_geometry_index_containing_geom_ids, 1);
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
