#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_FONT_FORMATS_H
#include "fontconfig/fontconfig.h"

#include "VG/openvg.h"
#include "fontinfo.h"
#include "shapes.h"

// Freetype library, set globally so I only open it once
static FT_Library ft_library = NULL;

typedef struct scoord_T {
        short x, y;
} scoord_T;

typedef struct paths_T {
        int cpos;
        int spos;
        int max_coords;
        int max_segments;
        scoord_T *coords;
        VGubyte *segments;
        int error;
} paths_T;

// Pre-allocate space for 1024 points and 256 segments. This should be
// plenty for normal use. Returns 0 if there wasn't any memory available.
static int alloc_paths(paths_T *paths)
{
        paths->cpos = paths->spos = 0;
        paths->coords = malloc(1024 * sizeof *paths->coords);
        if (paths->coords) {
                paths->max_coords = 1024;
                paths->segments = malloc(256 * sizeof *paths->segments);
                if (paths->segments) {
                        paths->max_segments = 256;
                        return -1;
                }
                free(paths->coords);
        }
        return 0;
}

static void free_paths(paths_T *paths)
{
        if (paths->coords)
                free(paths->coords);
        if (paths->segments)
                free(paths->segments);
}

static int add_path_coords(paths_T *path, int num)
{
        int size = path->max_coords;
        int cpos = path->cpos;
        path->cpos += num;
        if (path->cpos >= size) {
                scoord_T *mem = realloc(path->coords, (size + 256) * sizeof *path->coords);
                if (mem) {
                        path->coords = mem;
                        path->max_coords = size + 256;
                }
                else {
                        cpos = -1;
                }
        }
        return cpos;
}

static int add_path_segments(paths_T *path, int num)
{
        int size = path->max_segments;
        int spos = path->spos;
        path->spos += num;
        if (path->spos >= size) {
                VGubyte *mem = realloc(path->segments, (size + 64) * sizeof *path->segments);
                if (mem) {
                        path->segments = mem;
                        path->max_segments = size + 64;
                }
                else {
                        spos = -1;
                }
        }
        return spos;
}

static int ft_move_to(const FT_Vector *to, paths_T *outline)
{
        int spos = add_path_segments(outline, 1);
        if (spos < 0)
                return -1;
        outline->segments[spos] = VG_MOVE_TO;
        
        int cpos = add_path_coords(outline, 1);
        if (cpos < 0)
                return -1;
        outline->coords[cpos].x = to->x;
        outline->coords[cpos].y = to->y;
        return 0;
}

static int ft_line_to(const FT_Vector *to, paths_T *outline)
{
        int spos = add_path_segments(outline, 1);
        if (spos < 0)
                return -1;
        outline->segments[spos] = VG_LINE_TO;

        int cpos = add_path_coords(outline, 1);
        if (cpos < 0)
                return -1;
        outline->coords[cpos].x = to->x;
        outline->coords[cpos].y = to->y;
        return 0;
}

static int ft_conic_to(const FT_Vector *control, const FT_Vector *to,
                       paths_T *outline)
{
        int spos = add_path_segments(outline, 1);
        if (spos < 0)
                return -1;
        outline->segments[spos] = VG_QUAD_TO;
        
        int cpos = add_path_coords(outline, 2);
        if (cpos < 0)
                return -1;
        outline->coords[cpos].x = control->x;
        outline->coords[cpos].y = control->y;
        outline->coords[cpos+1].x = to->x;
        outline->coords[cpos+1].y = to->y;
        return 0;
}

static int ft_cubic_to(const FT_Vector *ctrl1, const FT_Vector *ctrl2,
                        const FT_Vector *to, paths_T *outline)
{
        int spos = add_path_segments(outline, 1);
        if (spos < 0)
                return -1;
        outline->segments[spos] = VG_CUBIC_TO;

        int cpos = add_path_coords(outline, 3);
        if (cpos < 0)
                return -1;
        outline->coords[cpos].x = ctrl1->x;
        outline->coords[cpos].y = ctrl1->y;
        outline->coords[cpos+1].x = ctrl2->x;
        outline->coords[cpos+1].y = ctrl2->y;
        outline->coords[cpos+2].x = to->x;
        outline->coords[cpos+2].y = to->y;
        return 0;
}

FT_Outline_Funcs funcs = {
        (FT_Outline_MoveTo_Func)&ft_move_to,
        (FT_Outline_LineTo_Func)&ft_line_to,
        (FT_Outline_ConicTo_Func)&ft_conic_to,
        (FT_Outline_CubicTo_Func)&ft_cubic_to,
        0, 0
};

// LoadTTFFile() loads a TTF from named file
Fontinfo LoadTTFFile(const char *filename)
{
        int error = 0;
        if (ft_library == NULL) {
                if (FT_Init_FreeType(&ft_library))
                        return NULL;
        }
	FT_Face face;
        int faceIndex = 0;
	if (FT_New_Face(ft_library, filename, faceIndex, &face))
                return NULL;

                // TODO: Fail loading bitmap fonts for now.
        if (!FT_IS_SCALABLE(face)) {
                FT_Done_Face(face);
                return NULL;
        }

                // Check to see if we're a PS font, if so try to load
                // a metric file (for kerning data).
        const char *format = FT_Get_Font_Format(face);
        if (!strcmp(format, "Type 1")) {
                char fname[strlen(filename)+5];
                strcpy(fname, filename);
                char *suffix = (char*)strrchr(fname, '.' );
                int   has_extension = suffix &&
                        (strcasecmp( suffix, ".pfa" ) == 0 ||
                         strcasecmp( suffix, ".pfb" ) == 0 );

                if (!has_extension)
                        suffix = (char*)fname + strlen(fname);

                memcpy(suffix, ".afm", 5);
                if (FT_Attach_File(face, fname)) {
                        memcpy(suffix, ".pfm", 5);
                        FT_Attach_File(face, fname);
                }
        }
        
        FT_Set_Char_Size(
              face,   // handle to face object
              64*64,  // char_width in 1/64th of points
              0,      // char_height in 1/64th of points
              96,     // horizontal device resolution
              96 );   // vertical device resolution

        Fontinfo font = calloc(1, sizeof *font);
        if (font == NULL) {
                FT_Done_Face(face);
                return NULL;
        }
        font->face = (void*)face;
        font->Name = face->family_name;
        font->Style = face->style_name;
        font->DescenderHeight = (VGfloat)face->size->metrics.descender /4096.0f;
        font->AscenderHeight = (VGfloat)face->size->metrics.ascender / 4096.f;
        font->Height = (VGfloat)face->size->metrics.height / 4096.f;
        font->Kerning = FT_HAS_KERNING(face);
        
        paths_T paths;
        if (!alloc_paths(&paths)) {
                FT_Done_Face(face);
                free(font);
                return NULL;
        }

        FT_Long numGlyphs = face->num_glyphs;
        font->Count = numGlyphs;
        font->CharacterMap = NULL;  // Indicate that we use FT's charmap
        font->vgfont = vgCreateFont(numGlyphs);
        if (font->vgfont == VG_INVALID_HANDLE) {
                free_paths(&paths);
                FT_Done_Face(face);
                free(font);
                return NULL;
        }

        VGfloat origin[2] = { 0.0f, 0.0f };
        VGfloat escapement[2] = { 0.0f, 0.0f };
        int cc;
        for (cc = 0; cc < numGlyphs; cc++) {
                if (!FT_Load_Glyph(face, cc, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING | FT_LOAD_IGNORE_TRANSFORM)) {
                        escapement[0] = (float)face->glyph->linearHoriAdvance / (64.0f*65536.0f);
                        paths.cpos = paths.spos = 0;
                        FT_Outline *outline = &face->glyph->outline;
                        if (FT_Outline_Decompose(outline, &funcs, &paths)) {
                                error = 1;
                                break;
                        }
                        VGPath path;
                        if (paths.spos) {
                                path = vgCreatePath(VG_PATH_FORMAT_STANDARD,
                                                    VG_PATH_DATATYPE_S_16,
                                                    1.0f/4096.0f, 0.0f, paths.spos, paths.cpos,
                                                    VG_PATH_CAPABILITY_APPEND_TO);
                                if (path == VG_INVALID_HANDLE) {
                                        error = 1;
                                        break;
                                }
                                vgAppendPathData(path, paths.spos, paths.segments, paths.coords);
                        }
                        else
                                path = VG_INVALID_HANDLE;
                        vgSetGlyphToPath(font->vgfont, cc, path, VG_FALSE, origin, escapement);
                        if (path != VG_INVALID_HANDLE)
                                vgDestroyPath(path);
                }
        }
        free_paths(&paths);
        if (error) {
                UnloadTTF(font);
                font = NULL;
        }
        
        return font;
}

// UnloadTTF unloads a font created by LoadTTF
void UnloadTTF(Fontinfo f)
{
        if (f) {
                if (f->face)
                        FT_Done_Face(f->face);
                if (f->vgfont)
                        vgDestroyFont(f->vgfont);
                free(f);
        }
}

// LoadTTF() - Loads a font given a name.
//   name is e.g. "DejaVu:monospace"
Fontinfo LoadTTF(const char *name)
{
        Fontinfo font = NULL;

        FcConfig *fc_config = FcInitLoadConfigAndFonts();
        if (fc_config) {
                FcPattern *pattern = FcNameParse((FcChar8*)name);
                if (pattern) {
                        FcConfigSubstitute(fc_config, pattern, FcMatchPattern);
                        FcDefaultSubstitute(pattern);
                        
                        FcResult result;
                        FcPattern *match = FcFontMatch(fc_config, pattern, &result);
                        if (match) {
                                FcChar8 *file = NULL;
                                if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
                                        font = LoadTTFFile((const char*)file);
                                }
                                FcPatternDestroy(match);
                        }
                        FcPatternDestroy(pattern);
                }
                FcConfigDestroy(fc_config);
        }
        return font;
}

// closeFontSystem() - Close and free data used by freetype2
void font_CloseFontSystem()
{
        if (ft_library != NULL) {
                FT_Done_FreeType(ft_library);
                ft_library = NULL;
        }
}

// Converting character codes to glyph codes. Also fills in kerning
// data between this and "prev" glyph if kern is non-null.
// kern is a two element array of floats.
unsigned int font_CharToGlyph(void *face, unsigned long code)
{
        unsigned int glyph = (unsigned int)FT_Get_Char_Index((FT_Face)face, (FT_ULong)code);
        return glyph;
}


void font_KernData(void *face, unsigned long curr, unsigned long prev,
                   VGfloat *kernX, VGfloat *kernY) 
{
        if (kernX) {
                if (prev != 0xffffffff) {
                        FT_Vector kern;
                        FT_Get_Kerning((FT_Face)face, prev, curr, FT_KERNING_DEFAULT, &kern);
                        *kernX = (VGfloat)kern.x / 4096.f;
                        *kernY = (VGfloat)kern.y / 4096.f;
                }
                else {
                        *kernX = 0.0f;
                        *kernY = 0.0f;
                }
        }
}

// FontKerning sets whether font will have kerning enabled.
//   If the font doesn't have kerning data then it will have no
//   effect. Doesn't work on the old font system (loadfont, default fonts).
void FontKerning(Fontinfo f, int value)
{
        if (f) {
                if ((f->face) && value) {
                        FT_Face face = (FT_Face)(f->face);
                        f->Kerning = FT_HAS_KERNING(face);
                }
                else
                        f->Kerning = 0;
        }
}