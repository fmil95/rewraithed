/*
 * Raziel's and Kain's models from the Dreamcast version, drawn with their
 * own 256 x 256 textures.
 *
 * With "Use DC models" on (DreamcastModels in rewraithed.ini), they are read
 * from the user's Dreamcast disc, a GDI dump in the "dreamcast" folder next
 * to the game (port/shared/dc_disc.c): the two object files from its
 * BIGFILE.DAT, the textures they use from TEXTURES.VQ. When the game has
 * loaded one of these objects
 * (STREAM_LoadObjectReturn, through a fixup), its model is replaced by the
 * Dreamcast one, which has the same skeleton (segments), so the game's own
 * animations drive it.
 *
 * The Dreamcast object files have the game's layout; only the texture
 * records of the faces differ. There, a record holds three UVs as the top
 * halves of floats (0 to 1) and a texture number, which is the 0x4800-byte
 * slot of TEXTURES.VQ at number * 0x4800. Each record is rewritten in place
 * as a PSX TextureMT3, which the drawing code copies into the primitives:
 * UVs 0-255 over the 256 x 256 texture, and a texture page with bit 15 set
 * and the port's texture number in its low 5 bits. PsyCross draws such
 * primitives from that texture (port/psycross/psyx_port.cpp,
 * PsyX_PortExtTexture) instead of from VRAM. The replacement model lives
 * outside the game's memory, so the object relocation and the VRAM moves
 * skip it (port_dc_is_external_model, fixups in MEMPACK.c and VRAM.c).
 */
#include "common.h"
#include "port/memory_map.h"
#include "settings.h"
#include "dc_disc.h"
#include "Game/OBTABLE.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* bigfile hashes of the two objects on the Dreamcast disc (its names do not
 * hash as the game's do), and the name each object file carries */
static const struct
{
    const char *name;
    unsigned long hash;
    const char *tag;
} dc_objects[] = {
    { "raziel", 0x90850620, "raziel__" },
    { "kain", 0x797909D0, "kain____" },
};

/* port/psycross/psyx_port.cpp */
void PsyX_PortSetExtTexture(int slot, const unsigned char *rgba, int width, int height);

#define DC_TEXTURE_SIZE   256
#define DC_TEXTURE_SLOT   0x4800
#define DC_MAX_TEXTURES   32
#define DC_MAX_MODELS     4

typedef struct
{
    char name[16];
    int tried;              /* the Dreamcast model was looked for */
    int failed;             /* not there, or not usable: not tried again */
    Model *model;
    Model *psx;             /* the object's own model, for F11 */
    int collisionReported;  /* hit shapes compared in the log */
} DcModel;

static DcModel dc_models[DC_MAX_MODELS];
static int dc_model_count;

static int dc_texture_ids[DC_MAX_TEXTURES];    /* Dreamcast number of each port slot */
static int dc_texture_count;

int port_dc_is_external_model(Model *model)
{
    int i;

    for (i = 0; i < dc_model_count; i++)
    {
        if ((dc_models[i].model != NULL) && (dc_models[i].model == model))
        {
            return 1;
        }
    }

    return 0;
}

/* the Dreamcast disc, opened the first time a model is wanted */
static DcDisc *dc_disc;
static int dc_disc_tried;

static DcDisc *dc_open_disc(void)
{
    char gdi[600];
    char problem[700];

    if (!dc_disc_tried)
    {
        dc_disc_tried = 1;

        if (!dc_disc_locate(gdi, sizeof(gdi), problem, sizeof(problem)))
        {
            printf("[rewraithed] Dreamcast models: %s\n", problem);
        }
        else if ((dc_disc = dc_disc_open(gdi)) == NULL)
        {
            printf("[rewraithed] Dreamcast models: %s is not a readable Soul Reaver disc\n", gdi);
        }
        else
        {
            printf("[rewraithed] Dreamcast models from %s\n", gdi);
        }
    }

    return dc_disc;
}

/* Morton order, x in the even bits and y in the odd: the order of the
 * blocks in these textures (checked against the models' UVs) */
static int twiddle(int x, int y)
{
    int v = 0;
    int i;

    for (i = 0; i < 8; i++)
    {
        v |= ((x >> i) & 1) << (2 * i);
        v |= ((y >> i) & 1) << (2 * i + 1);
    }

    return v;
}

/* a 256 x 256 VQ texture (ARGB1555 codebook of 2x2 blocks) to RGBA */
static void decode_vq(const unsigned char *slot, unsigned char *rgba)
{
    const unsigned char *book = slot;
    const unsigned char *index = slot + 2048;
    int bx;
    int by;
    int k;

    for (by = 0; by < DC_TEXTURE_SIZE / 2; by++)
    {
        for (bx = 0; bx < DC_TEXTURE_SIZE / 2; bx++)
        {
            int entry = index[twiddle(bx, by)];

            /* the block's texels, in twiddled order (x first) */
            for (k = 0; k < 4; k++)
            {
                int x = bx * 2 + (k & 1);
                int y = by * 2 + (k >> 1);
                unsigned int c = book[(entry * 4 + k) * 2] | (book[(entry * 4 + k) * 2 + 1] << 8);
                unsigned char *p = &rgba[(y * DC_TEXTURE_SIZE + x) * 4];

                p[0] = (unsigned char)(((c >> 10) & 31) * 255 / 31);
                p[1] = (unsigned char)(((c >> 5) & 31) * 255 / 31);
                p[2] = (unsigned char)((c & 31) * 255 / 31);
                p[3] = (c & 0x8000) ? 255 : 0;
            }
        }
    }
}

/* the port's texture number for a Dreamcast one, loading it the first time;
 * -1 if it cannot be loaded */
static int dc_texture_slot(int id)
{
    unsigned char *slot;
    unsigned char *rgba;
    int i;

    for (i = 0; i < dc_texture_count; i++)
    {
        if (dc_texture_ids[i] == id)
        {
            return i;
        }
    }

    if (dc_texture_count >= DC_MAX_TEXTURES)
    {
        return -1;
    }

    slot = (unsigned char *)malloc(DC_TEXTURE_SLOT);

    if ((slot == NULL) || (dc_open_disc() == NULL) || !dc_disc_read_textures(dc_disc, (long)id * DC_TEXTURE_SLOT, DC_TEXTURE_SLOT, slot))
    {
        printf("[rewraithed] Dreamcast texture %d missing\n", id);
        free(slot);
        return -1;
    }

    rgba = (unsigned char *)malloc(DC_TEXTURE_SIZE * DC_TEXTURE_SIZE * 4);

    if (rgba == NULL)
    {
        free(slot);
        return -1;
    }

    decode_vq(slot, rgba);
    PsyX_PortSetExtTexture(dc_texture_count, rgba, DC_TEXTURE_SIZE, DC_TEXTURE_SIZE);

    free(slot);
    free(rgba);

    dc_texture_ids[dc_texture_count] = id;

    return dc_texture_count++;
}

/* a UV stored as the top half of a float, to 0-255 over the texture */
static unsigned char dc_uv(unsigned short half)
{
    unsigned int bits = (unsigned int)half << 16;
    float f;
    int v;

    memcpy(&f, &bits, sizeof(f));
    v = (int)floorf(f * DC_TEXTURE_SIZE);

    return (unsigned char)((v < 0) ? 0 : ((v > 255) ? 255 : v));
}

/* a Dreamcast texture record, rewritten in place as a PSX one */
static int dc_convert_texture(TextureMT3 *texture)
{
    unsigned short words[7];
    int slot;

    memcpy(words, texture, sizeof(words));

    slot = dc_texture_slot(words[6] & 0x1FFF);

    if (slot < 0)
    {
        return 0;
    }

    texture->u0 = dc_uv(words[0]);
    texture->v0 = dc_uv(words[1]);
    texture->u1 = dc_uv(words[2]);
    texture->v1 = dc_uv(words[3]);
    texture->u2 = dc_uv(words[4]);
    texture->v2 = dc_uv(words[5]);
    texture->clut = 0;

    /* 16-bit texture page (the GPU needs no palette); bit 15 and the slot
     * send it to the port's texture */
    texture->tpage = (unsigned short)(0x8000 | (2 << 7) | slot);
    texture->pad1 = 0;
    texture->sortPush = 0;

    /* the colour lighting starts from (GPU code of a lit textured triangle) */
    texture->color = 0x34808080;

    return 1;
}

/* the segment a vertex belongs to, -1 if none */
static int dc_vertex_segment(Model *model, int vertex)
{
    int s;

    for (s = 0; s < model->numSegments; s++)
    {
        Segment *segment = &model->segmentList[s];

        if ((vertex >= segment->firstVertex) && (vertex <= segment->lastVertex))
        {
            return s;
        }
    }

    return -1;
}

/* Faces drawn from both sides. The Dreamcast's own renderer draws these
 * models without culling, and they were made for it; the PSX drawing code
 * culls faces that turn their back to the camera, and holes opened:
 *  - faces with flag 0x10 (drawn from both sides there, skipped outright by
 *    the PSX drawing code);
 *  - faces joining two segments, which fold over where a joint bends;
 *  - the legs' faces (segments 6 to 13): the back of Raziel's right calf
 *    opened with the others two-sided.
 * Not the rest: the body under the cloak is modelled beneath it, and drawn
 * from both sides its inner side showed through around the neck.
 * Each becomes itself plus a copy wound the other way round with its UVs to
 * match, so that one of the two always faces the camera. The face list is
 * rebuilt, larger, in the port data area (the primitive pools are larger to
 * match, GAMELOOP.c fixups). */
#define DC_LEG_FIRST 6
#define DC_LEG_LAST  13

static int dc_two_sided(Model *model, MFace *face)
{
    int s0;
    int s1;
    int s2;

    if (face->flags & 0x10)
    {
        return 1;
    }

    s0 = dc_vertex_segment(model, face->face.v0);
    s1 = dc_vertex_segment(model, face->face.v1);
    s2 = dc_vertex_segment(model, face->face.v2);

    if ((s1 != s0) || (s2 != s0))
    {
        return 1;
    }

    return (s0 >= DC_LEG_FIRST) && (s0 <= DC_LEG_LAST);
}

/* Raziel's scarf and collar (segments 21 to 27, around his neck) are drawn a
 * little after what lies under them. The Dreamcast draws with a depth
 * buffer, so the chest and shoulders modelled just beneath them never show;
 * the PSX drawing code sorts whole triangles by their average depth, and
 * bits of those bones came through the collar. A face's texture record
 * carries a sort push (TextureMT3.sortPush, eight ordering table steps
 * each): these faces get their own copy of their record with a push towards
 * the camera. */
#define DC_COLLAR_FIRST 21
#define DC_COLLAR_LAST  27
#define DC_COLLAR_PUSH  (-3)

static int dc_in_collar(Model *model, int vertex)
{
    int s = dc_vertex_segment(model, vertex);

    return (s >= DC_COLLAR_FIRST) && (s <= DC_COLLAR_LAST);
}

static void dc_push_collar(Model *model, const char *name)
{
    TextureMT3 *copies;
    long count = 0;
    long n = 0;
    long i;

    if ((_stricmp(name, "raziel") != 0) || (model->numSegments <= DC_COLLAR_LAST))
    {
        return;
    }

    for (i = 0; i < model->numFaces; i++)
    {
        MFace *face = &model->faceList[i];

        if ((face->flags & 0x2) && (dc_in_collar(model, face->face.v0) || dc_in_collar(model, face->face.v1) || dc_in_collar(model, face->face.v2)))
        {
            count++;
        }
    }

    if (count == 0)
    {
        return;
    }

    copies = (TextureMT3 *)port_extra_alloc(count * sizeof(TextureMT3));

    if (copies == NULL)
    {
        return;
    }

    for (i = 0; i < model->numFaces; i++)
    {
        MFace *face = &model->faceList[i];

        if ((face->flags & 0x2) && (dc_in_collar(model, face->face.v0) || dc_in_collar(model, face->face.v1) || dc_in_collar(model, face->face.v2)))
        {
            copies[n] = *(TextureMT3 *)face->color;
            copies[n].sortPush = DC_COLLAR_PUSH;
            face->color = (long)(intptr_t)&copies[n];
            n++;
        }
    }
}

static void dc_double_sided_faces(Model *model, const char *name)
{
    MFace *faces;
    TextureMT3 *textures;
    unsigned char *twoSided;
    long count = 0;
    long n;
    long i;

    twoSided = (unsigned char *)calloc(model->numFaces, 1);

    if (twoSided == NULL)
    {
        return;
    }

    for (i = 0; i < model->numFaces; i++)
    {
        if (dc_two_sided(model, &model->faceList[i]))
        {
            twoSided[i] = 1;
            count++;
        }
    }

    if (count == 0)
    {
        free(twoSided);
        return;
    }

    faces = (MFace *)port_extra_alloc((model->numFaces + count) * sizeof(MFace));
    textures = (TextureMT3 *)port_extra_alloc(count * sizeof(TextureMT3));

    if ((faces == NULL) || (textures == NULL))
    {
        printf("[rewraithed] Dreamcast %s: no room for its two-sided faces\n", name);
        free(twoSided);
        return;
    }

    memcpy(faces, model->faceList, model->numFaces * sizeof(MFace));
    n = model->numFaces;

    for (i = 0; i < model->numFaces; i++)
    {
        MFace *face = &faces[i];
        MFace *back;

        if (!twoSided[i])
        {
            continue;
        }

        face->flags &= ~0x10;

        back = &faces[n++];
        *back = *face;
        back->face.v1 = face->face.v2;
        back->face.v2 = face->face.v1;

        if (face->flags & 0x2)
        {
            TextureMT3 *texture = &textures[(back - faces) - model->numFaces];

            *texture = *(TextureMT3 *)face->color;
            texture->u1 = ((TextureMT3 *)face->color)->u2;
            texture->v1 = ((TextureMT3 *)face->color)->v2;
            texture->u2 = ((TextureMT3 *)face->color)->u1;
            texture->v2 = ((TextureMT3 *)face->color)->v1;
            back->color = (long)(intptr_t)texture;
        }
    }

    model->faceList = faces;
    model->numFaces = n;

    free(twoSided);
}

static int dc_contains(const unsigned char *data, long size, const char *text)
{
    long n = (long)strlen(text);
    long i;

    for (i = 0; i + n <= size; i++)
    {
        if (memcmp(data + i, text, n) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static Model *dc_load_model(const char *name)
{
    unsigned char *raw;
    unsigned char *data;
    unsigned char *converted;
    Object *object;
    Model *model;
    long size;
    long numRelocs;
    long table;
    long i;

    raw = NULL;

    if (dc_open_disc() == NULL)
    {
        return NULL;
    }

    for (i = 0; i < (long)(sizeof(dc_objects) / sizeof(dc_objects[0])); i++)
    {
        if (_stricmp(dc_objects[i].name, name) == 0)
        {
            raw = dc_disc_read_bigfile(dc_disc, dc_objects[i].hash, &size);

            /* the object's own name inside: another release of the disc
             * may have other files at that hash */
            if ((raw != NULL) && !dc_contains(raw, size, dc_objects[i].tag))
            {
                printf("[rewraithed] Dreamcast %s: not found on this disc\n", name);
                free(raw);
                raw = NULL;
            }
        }
    }

    if (raw == NULL)
    {
        return NULL;
    }

    /* the relocation table (LOAD_RelocBinaryData): a count, the offsets of
     * the pointers in the data, padded to whole 2048-byte blocks */
    numRelocs = *(long *)raw;
    table = ((numRelocs + 512) / 512) * 512 * 4;

    if ((numRelocs <= 0) || (table >= size))
    {
        free(raw);
        return NULL;
    }

    /* at a PSX-style address: the drawing code tells texture pointers from
     * colours by their sign */
    data = (unsigned char *)port_extra_alloc(size - table);

    if (data == NULL)
    {
        printf("[rewraithed] Dreamcast %s: no room left for it\n", name);
        free(raw);
        return NULL;
    }

    memcpy(data, raw + table, size - table);

    for (i = 0; i < numRelocs; i++)
    {
        long offset = ((long *)raw)[1 + i];

        if ((offset >= 0) && (offset + 4 <= size - table))
        {
            *(intptr_t *)(data + offset) += (intptr_t)data;
        }
    }

    free(raw);

    object = (Object *)data;

    if (object->numModels < 1)
    {
        return NULL;
    }

    model = object->modelList[0];

    /* each record once: faces share them */
    converted = (unsigned char *)calloc((model->endTextures - model->startTextures) + 1, 1);

    for (i = 0; i < model->numFaces; i++)
    {
        MFace *face = &model->faceList[i];
        TextureMT3 *texture = (TextureMT3 *)face->color;
        long index;

        if (!(face->flags & 0x2) || (texture < model->startTextures) || (texture >= model->endTextures))
        {
            continue;
        }

        index = texture - model->startTextures;

        if ((converted != NULL) && !converted[index])
        {
            converted[index] = 1;

            if (!dc_convert_texture(texture))
            {
                free(converted);
                printf("[rewraithed] Dreamcast %s: a texture is missing, keeping the PlayStation model\n", name);
                return NULL;
            }
        }
    }

    free(converted);

    /* Lit from the vertex normals, as the PlayStation model's faces are (flag
     * 0x1, smooth shading). Without it the drawing code lit each face flat
     * from its face normal, a darker path; the Dreamcast vertex normals are
     * the game's own normal table, as the PlayStation ones. */
    for (i = 0; i < model->numFaces; i++)
    {
        model->faceList[i].flags |= 0x1;
    }

    dc_push_collar(model, name);
    dc_double_sided_faces(model, name);

    /* the rest of the object file (animations, scripts) stays unused */
    return model;
}

/* the Dreamcast model for one of the two objects, loaded the first time */
static void dc_try_load(DcModel *dc, Model *psx)
{
    if (dc->tried)
    {
        return;
    }

    dc->tried = 1;
    dc->model = dc_load_model(dc->name);
    dc->failed = (dc->model == NULL);

    if ((dc->model != NULL) && (dc->model->numSegments != psx->numSegments))
    {
        printf("[rewraithed] Dreamcast %s has %ld segments, not %ld: not used\n", dc->name,
            (long)dc->model->numSegments, (long)psx->numSegments);
        dc->failed = 1;
    }

    if (!dc->failed)
    {
        printf("[rewraithed] Dreamcast model for %s: %ld vertices, %ld faces\n", dc->name,
            (long)dc->model->numVertices, (long)dc->model->numFaces);
    }
}

/* the hit spheres, faces and boxes of a model (HInfo of its segments) */
static long dc_count_hprims(Model *model)
{
    long count = 0;
    long s;

    for (s = 0; s < model->numSegments; s++)
    {
        HInfo *h = model->segmentList[s].hInfo;

        if (h != NULL)
        {
            count += h->numHFaces + h->numHSpheres + h->numHBoxes;
        }
    }

    return count;
}

/* The Dreamcast model only changes how they look: what can hit them and be
 * hit (the hit spheres of each segment, which INSTANCE_DefaultInit copies
 * per instance) stays the PlayStation model's, as the game's attacks,
 * Kain's fight among them, were tuned for those. The Dreamcast object file
 * carries its own (the same within a unit or so). The skeletons are the same, segment for
 * segment; done at each load, as the PlayStation model moves with its
 * object's memory. */
static void dc_use_psx_collision(DcModel *dc)
{
    Model *dcModel = dc->model;
    Model *psx = dc->psx;
    long s;

    if ((dcModel == NULL) || (psx == NULL) || (dcModel->numSegments != psx->numSegments))
    {
        return;
    }

    if (!dc->collisionReported)
    {
        dc->collisionReported = 1;

        printf("[rewraithed] Dreamcast %s: hit shapes %ld, PlayStation %ld; the PlayStation ones are used\n",
            dc->name, dc_count_hprims(dcModel), dc_count_hprims(psx));
    }

    for (s = 0; s < dcModel->numSegments; s++)
    {
        dcModel->segmentList[s].hInfo = psx->segmentList[s].hInfo;
    }
}

/* STREAM_LoadObjectReturn: an object has been loaded */
void port_dc_object_loaded(char *name, Object *object)
{
    DcModel *dc = NULL;
    int i;

    /* a level's object list may spell the name in either case (the game
     * compares object names with strcmpi) */
    if ((_stricmp(name, "raziel") != 0) && (_stricmp(name, "kain") != 0))
    {
        return;
    }

    if ((object == NULL) || (object->numModels < 1))
    {
        return;
    }

    for (i = 0; i < dc_model_count; i++)
    {
        if (_stricmp(dc_models[i].name, name) == 0)
        {
            dc = &dc_models[i];
        }
    }

    if (dc == NULL)
    {
        if (dc_model_count >= DC_MAX_MODELS)
        {
            return;
        }

        dc = &dc_models[dc_model_count++];
        memset(dc, 0, sizeof(*dc));
        snprintf(dc->name, sizeof(dc->name), "%s", name);
    }

    /* kept even with the models off, so that F11 can turn them on */
    dc->psx = object->modelList[0];

    if (!port_settings.dcModels)
    {
        return;
    }

    dc_try_load(dc, dc->psx);

    if (!dc->failed)
    {
        dc_use_psx_collision(dc);
        object->modelList[0] = dc->model;
    }
}

/* F11 (video_hotkeys.c): the Dreamcast models on or off while playing. The
 * objects are looked up again, as Kain's may have gone with its level. */
void port_dc_set_enabled(int on)
{
    int i;

    port_settings.dcModels = on;

    for (i = 0; i < dc_model_count; i++)
    {
        DcModel *dc = &dc_models[i];
        char objectName[9];
        Object *object;
        size_t n = strlen(dc->name);

        /* object names are 8 characters, padded with '_' */
        memset(objectName, '_', 8);
        objectName[8] = 0;
        memcpy(objectName, dc->name, (n < 8) ? n : 8);

        for (n = 0; n < 8; n++)
        {
            objectName[n] = (char)tolower((unsigned char)objectName[n]);
        }

        object = OBTABLE_FindObject(objectName);

        if ((object == NULL) || (object->numModels < 1) || (dc->psx == NULL))
        {
            continue;
        }

        if (on)
        {
            dc_try_load(dc, dc->psx);

            if (!dc->failed)
            {
                object->modelList[0] = dc->model;
            }
        }
        else
        {
            object->modelList[0] = dc->psx;
        }
    }
}
