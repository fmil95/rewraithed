/*
 * Raziel's and Kain's models from the Dreamcast version, drawn with their
 * own 256 x 256 textures.
 *
 * tools/dc_import.py writes, into a "dreamcast" folder next to the game,
 * the two Dreamcast object files (raziel.pcm, kain.pcm) and the textures
 * they use (texNNNN.vq). When the game has loaded one of these objects
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


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void port_exe_folder(char *out, size_t size);   /* native/disc_folder_win32.c */

/* port/psycross/psyx_port.cpp */
void PsyX_PortSetExtTexture(int slot, const unsigned char *rgba, int width, int height);

#define DC_TEXTURE_SIZE   256
#define DC_TEXTURE_SLOT   0x4800
#define DC_MAX_TEXTURES   32
#define DC_MAX_MODELS     4

typedef struct
{
    char name[16];
    int failed;             /* not there, or not usable: not tried again */
    Model *model;
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

static void dc_path(char *out, size_t size, const char *file)
{
    char exe[512];

    port_exe_folder(exe, sizeof(exe));
    snprintf(out, size, "%s\\dreamcast\\%s", exe, file);
}

static unsigned char *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;

    if (f == NULL)
    {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (unsigned char *)malloc(*size);

    if ((data != NULL) && (fread(data, 1, *size, f) != (size_t)*size))
    {
        free(data);
        data = NULL;
    }

    fclose(f);

    return data;
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
    char file[32];
    char path[600];
    unsigned char *slot;
    unsigned char *rgba;
    long size;
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

    snprintf(file, sizeof(file), "tex%04d.vq", id);
    dc_path(path, sizeof(path), file);
    slot = read_file(path, &size);

    if ((slot == NULL) || (size < DC_TEXTURE_SLOT))
    {
        printf("[rewraithed] Dreamcast texture %s missing\n", file);
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

/* Every face is drawn from both sides. The Dreamcast's own renderer draws
 * these models without culling, and they were made for it: some faces are
 * wound the wrong way round (the back of Raziel's right calf), others have
 * flag 0x10 (drawn from both sides there, skipped outright by the PSX
 * drawing code), and those joining two segments fold over where a joint
 * bends. The PSX drawing code culls faces that turn their back to the
 * camera, and holes opened. Each face becomes itself plus a copy wound the
 * other way round with its UVs to match, so that one of the two always
 * faces the camera. The face list is rebuilt, twice as long, in the port
 * data area (the primitive pools are larger to match, GAMELOOP.c fixups). */
static int dc_two_sided(Model *model, MFace *face)
{
    (void)model;
    (void)face;

    return 1;
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

static Model *dc_load_model(const char *name)
{
    char file[32];
    char path[600];
    unsigned char *raw;
    unsigned char *data;
    unsigned char *converted;
    Object *object;
    Model *model;
    long size;
    long numRelocs;
    long table;
    long i;

    snprintf(file, sizeof(file), "%s.pcm", name);
    dc_path(path, sizeof(path), file);
    raw = read_file(path, &size);

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

    dc_double_sided_faces(model, name);

    /* the rest of the object file (animations, scripts) stays unused */
    return model;
}

/* STREAM_LoadObjectReturn: an object has been loaded */
void port_dc_object_loaded(char *name, Object *object)
{
    DcModel *dc = NULL;
    Model *psx;
    int i;

    if ((strcmp(name, "raziel") != 0) && (strcmp(name, "kain") != 0))
    {
        return;
    }

    if ((object == NULL) || (object->numModels < 1))
    {
        return;
    }

    for (i = 0; i < dc_model_count; i++)
    {
        if (strcmp(dc_models[i].name, name) == 0)
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
        snprintf(dc->name, sizeof(dc->name), "%s", name);
        dc->model = dc_load_model(name);
        dc->failed = (dc->model == NULL);

        psx = object->modelList[0];

        if ((dc->model != NULL) && (dc->model->numSegments != psx->numSegments))
        {
            printf("[rewraithed] Dreamcast %s has %ld segments, not %ld: not used\n", name,
                (long)dc->model->numSegments, (long)psx->numSegments);
            dc->failed = 1;
        }

        if (!dc->failed)
        {
            printf("[rewraithed] Dreamcast model for %s: %ld vertices, %ld faces\n", name,
                (long)dc->model->numVertices, (long)dc->model->numFaces);
        }
    }

    if (!dc->failed)
    {
        object->modelList[0] = dc->model;
    }
}

