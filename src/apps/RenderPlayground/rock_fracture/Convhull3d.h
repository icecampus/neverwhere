/*
 Port of convhull_3d.h (https://github.com/leomccormack/convhull_3d)
 Original by Leo McCormack, 2017-2018, MIT License.
 Adapted to namespace rock_fracture, C++20 cleanup.
*/

#ifndef ROCK_FRACTURE_CONVHULL_3D_INCLUDED
#define ROCK_FRACTURE_CONVHULL_3D_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONVHULL_3D_USE_FLOAT_PRECISION
    typedef float CH_FLOAT;
#else
    typedef double CH_FLOAT;
#endif
    typedef struct _ch_vertex {
        union {
            CH_FLOAT v[3];
            struct {
                CH_FLOAT x, y, z;
            };
        };
    } ch_vertex;
    typedef ch_vertex ch_vec3;

    void convhull_3d_build(
        ch_vertex* const in_vertices,
        const int nVert,
        int** out_faces,
        int* nOut_faces);

    void convhull_3d_export_obj(
        ch_vertex* const vertices,
        const int nVert,
        int* const faces,
        const int nFaces,
        const int keepOnlyUsedVerticesFLAG,
        char* const obj_filename);

    void convhull_3d_export_m(
        ch_vertex* const vertices,
        const int nVert,
        int* const faces,
        const int nFaces,
        char* const m_filename);

    void extractVerticesFromObjFile(
        char* const obj_filename,
        ch_vertex** out_vertices,
        int* out_nVert);

#ifdef __cplusplus
}
#endif

#endif /* ROCK_FRACTURE_CONVHULL_3D_INCLUDED */


#ifdef CONVHULL_3D_ENABLE

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <float.h>
#include <ctype.h>
#include <string.h>
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define CV_STRNCPY(a,b,c) strncpy_s(a,c+1,b,c);
#define CV_STRCAT(a,b) strcat_s(a,sizeof(b),b);
#else
#define CV_STRNCPY(a,b,c) strncpy(a,b,c);
#define CV_STRCAT(a,b) strcat(a,b);
#endif
#ifdef CONVHULL_3D_USE_FLOAT_PRECISION
#define CH_FLT_MIN FLT_MIN
#define CH_FLT_MAX FLT_MAX
#define CH_NOISE_VAL 0.00001f
#else
#define CH_FLT_MIN DBL_MIN
#define CH_FLT_MAX DBL_MAX
#define CH_NOISE_VAL 0.0000001
#endif
#ifndef MIN
#define MIN(a,b) (( (a) < (b) ) ? (a) : (b) )
#endif
#ifndef MAX
#define MAX(a,b) (( (a) > (b) ) ? (a) : (b) )
#endif
#define CH_MAX_NUM_FACES 50000

typedef struct float_w_idx {
    CH_FLOAT val;
    int idx;
} float_w_idx;

typedef struct int_w_idx {
    int val;
    int idx;
} int_w_idx;

static int cmp_asc_float(const void* a, const void* b) {
    struct float_w_idx* a1 = (struct float_w_idx*)a;
    struct float_w_idx* a2 = (struct float_w_idx*)b;
    if ((*a1).val < (*a2).val) return -1;
    if ((*a1).val > (*a2).val) return 1;
    return 0;
}

static int cmp_desc_float(const void* a, const void* b) {
    struct float_w_idx* a1 = (struct float_w_idx*)a;
    struct float_w_idx* a2 = (struct float_w_idx*)b;
    if ((*a1).val > (*a2).val) return -1;
    if ((*a1).val < (*a2).val) return 1;
    return 0;
}

static int cmp_asc_int(const void* a, const void* b) {
    struct int_w_idx* a1 = (struct int_w_idx*)a;
    struct int_w_idx* a2 = (struct int_w_idx*)b;
    if ((*a1).val < (*a2).val) return -1;
    if ((*a1).val > (*a2).val) return 1;
    return 0;
}

static int cmp_desc_int(const void* a, const void* b) {
    struct int_w_idx* a1 = (struct int_w_idx*)a;
    struct int_w_idx* a2 = (struct int_w_idx*)b;
    if ((*a1).val > (*a2).val) return -1;
    if ((*a1).val < (*a2).val) return 1;
    return 0;
}

static void sort_float(
    CH_FLOAT* in_vec,
    CH_FLOAT* out_vec,
    int* new_idices,
    int len,
    int descendFLAG)
{
    int i;
    struct float_w_idx* data;
    data = (float_w_idx*)malloc(len * sizeof(float_w_idx));
    for (i = 0; i < len; i++) {
        data[i].val = in_vec[i];
        data[i].idx = i;
    }
    if (descendFLAG) qsort(data, len, sizeof(data[0]), cmp_desc_float);
    else qsort(data, len, sizeof(data[0]), cmp_asc_float);
    for (i = 0; i < len; i++) {
        if (out_vec != NULL) out_vec[i] = data[i].val;
        else in_vec[i] = data[i].val;
        if (new_idices != NULL) new_idices[i] = data[i].idx;
    }
    free(data);
}

static void sort_int(
    int* in_vec,
    int* out_vec,
    int* new_idices,
    int len,
    int descendFLAG)
{
    int i;
    struct int_w_idx* data;
    data = (int_w_idx*)malloc(len * sizeof(int_w_idx));
    for (i = 0; i < len; i++) {
        data[i].val = in_vec[i];
        data[i].idx = i;
    }
    if (descendFLAG) qsort(data, len, sizeof(data[0]), cmp_desc_int);
    else qsort(data, len, sizeof(data[0]), cmp_asc_int);
    for (i = 0; i < len; i++) {
        if (out_vec != NULL) out_vec[i] = data[i].val;
        else in_vec[i] = data[i].val;
        if (new_idices != NULL) new_idices[i] = data[i].idx;
    }
    free(data);
}

static ch_vec3 cross(ch_vec3* v1, ch_vec3* v2) {
    ch_vec3 c;
    c.x = v1->y * v2->z - v1->z * v2->y;
    c.y = v1->z * v2->x - v1->x * v2->z;
    c.z = v1->x * v2->y - v1->y * v2->x;
    return c;
}

static CH_FLOAT det_4x4(CH_FLOAT* m) {
    return
        m[3] * m[6] * m[9] * m[12] - m[2] * m[7] * m[9] * m[12] -
        m[3] * m[5] * m[10] * m[12] + m[1] * m[7] * m[10] * m[12] +
        m[2] * m[5] * m[11] * m[12] - m[1] * m[6] * m[11] * m[12] -
        m[3] * m[6] * m[8] * m[13] + m[2] * m[7] * m[8] * m[13] +
        m[3] * m[4] * m[10] * m[13] - m[0] * m[7] * m[10] * m[13] -
        m[2] * m[4] * m[11] * m[13] + m[0] * m[6] * m[11] * m[13] +
        m[3] * m[5] * m[8] * m[14] - m[1] * m[7] * m[8] * m[14] -
        m[3] * m[4] * m[9] * m[14] + m[0] * m[7] * m[9] * m[14] +
        m[1] * m[4] * m[11] * m[14] - m[0] * m[5] * m[11] * m[14] -
        m[2] * m[5] * m[8] * m[15] + m[1] * m[6] * m[8] * m[15] +
        m[2] * m[4] * m[9] * m[15] - m[0] * m[6] * m[9] * m[15] -
        m[1] * m[4] * m[10] * m[15] + m[0] * m[5] * m[10] * m[15];
}

static void plane_3d(CH_FLOAT* p, CH_FLOAT* c, CH_FLOAT* d) {
    int i, j, k, l;
    int r[3];
    CH_FLOAT sign, det, norm_c;
    CH_FLOAT pdiff[2][3], pdiff_s[2][2];

    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            pdiff[i][j] = p[(i + 1) * 3 + j] - p[i * 3 + j];
    memset(c, 0, 3 * sizeof(CH_FLOAT));
    sign = 1.0;
    for (i = 0; i < 3; i++) r[i] = i;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0, l = 0; k < 3; k++) {
                if (r[k] != i) {
                    pdiff_s[j][l] = pdiff[j][k];
                    l++;
                }
            }
        }
        det = pdiff_s[0][0] * pdiff_s[1][1] - pdiff_s[1][0] * pdiff_s[0][1];
        c[i] = sign * det;
        sign *= -1.0;
    }
    norm_c = (CH_FLOAT)0.0;
    for (i = 0; i < 3; i++) norm_c += (pow(c[i], 2.0));
    norm_c = sqrt(norm_c);
    for (i = 0; i < 3; i++) c[i] /= norm_c;
    (*d) = (CH_FLOAT)0.0;
    for (i = 0; i < 3; i++) (*d) += -p[i] * c[i];
}

static void ismember(
    int* pLeft,
    int* pRight,
    int* pOut,
    int nLeftElements,
    int nRightElements)
{
    int i, j;
    memset(pOut, 0, nLeftElements * sizeof(int));
    for (i = 0; i < nLeftElements; i++)
        for (j = 0; j < nRightElements; j++)
            if (pLeft[i] == pRight[j]) pOut[i] = 1;
}

void convhull_3d_build(
    ch_vertex* const in_vertices,
    const int nVert,
    int** out_faces,
    int* nOut_faces)
{
    int i, j, k, l, h;
    int nFaces, p, d;
    int* aVec, * faces;
    CH_FLOAT dfi, v, max_p, min_p;
    CH_FLOAT* points, * cf, * cfi, * df, * p_s, * span;

    if (nVert < 3 || in_vertices == NULL) {
        (*out_faces) = NULL;
        (*nOut_faces) = 0;
        return;
    }

    d = 3;
    span = (CH_FLOAT*)malloc(d * sizeof(CH_FLOAT));
    for (j = 0; j < d; j++) {
        max_p = 2.23e-13; min_p = 2.23e+13;
        for (i = 0; i < nVert; i++) {
            max_p = MAX(max_p, in_vertices[i].v[j]);
            min_p = MIN(min_p, in_vertices[i].v[j]);
        }
        span[j] = max_p - min_p;
    }
    points = (CH_FLOAT*)malloc(nVert * (d + 1) * sizeof(CH_FLOAT));
    for (i = 0; i < nVert; i++) {
        for (j = 0; j < d; j++)
            points[i * (d + 1) + j] = in_vertices[i].v[j] + CH_NOISE_VAL * ((CH_FLOAT)rand() / (CH_FLOAT)RAND_MAX);
        points[i * (d + 1) + d] = 1.0f;
    }

    nFaces = (d + 1);
    faces = (int*)calloc(nFaces * d, sizeof(int));
    aVec = (int*)malloc(nFaces * sizeof(int));
    for (i = 0; i < nFaces; i++) aVec[i] = i;

    cf = (CH_FLOAT*)malloc(nFaces * d * sizeof(CH_FLOAT));
    cfi = (CH_FLOAT*)malloc(d * sizeof(CH_FLOAT));
    df = (CH_FLOAT*)malloc(nFaces * sizeof(CH_FLOAT));
    p_s = (CH_FLOAT*)malloc(d * d * sizeof(CH_FLOAT));
    for (i = 0; i < nFaces; i++) {
        for (j = 0, k = 0; j < (d + 1); j++) {
            if (aVec[j] != i) {
                faces[i * d + k] = aVec[j];
                k++;
            }
        }
        for (j = 0; j < d; j++)
            for (k = 0; k < d; k++)
                p_s[j * d + k] = points[(faces[i * d + j]) * (d + 1) + k];

        plane_3d(p_s, cfi, &dfi);
        for (j = 0; j < d; j++) cf[i * d + j] = cfi[j];
        df[i] = dfi;
    }
    CH_FLOAT* A;
    int* bVec, * fVec, * asfVec, * face_tmp;

    bVec = (int*)malloc(4 * sizeof(int));
    for (i = 0; i < d + 1; i++) bVec[i] = i;

    A = (CH_FLOAT*)calloc((d + 1) * (d + 1), sizeof(CH_FLOAT));
    face_tmp = (int*)malloc((d + 1) * sizeof(int));
    fVec = (int*)malloc((d + 1) * sizeof(int));
    asfVec = (int*)malloc((d + 1) * sizeof(int));
    for (k = 0; k < (d + 1); k++) {
        for (i = 0; i < d; i++) fVec[i] = faces[k * d + i];
        sort_int(fVec, NULL, NULL, d, 0);
        p = k;
        for (i = 0; i < d; i++)
            for (j = 0; j < (d + 1); j++)
                A[i * (d + 1) + j] = points[(faces[k * d + i]) * (d + 1) + j];
        for (; i < (d + 1); i++)
            for (j = 0; j < (d + 1); j++)
                A[i * (d + 1) + j] = points[p * (d + 1) + j];

        v = det_4x4(A);

        if (v < 0) {
            for (j = 0; j < d; j++) face_tmp[j] = faces[k * d + j];
            for (j = 0, l = d - 2; j < d - 1; j++, l++)
                faces[k * d + l] = face_tmp[d - j - 1];

            for (j = 0; j < d; j++) cf[k * d + j] = -cf[k * d + j];
            df[k] = -df[k];
            for (i = 0; i < d; i++)
                for (j = 0; j < (d + 1); j++)
                    A[i * (d + 1) + j] = points[(faces[k * d + i]) * (d + 1) + j];
            for (; i < (d + 1); i++)
                for (j = 0; j < (d + 1); j++)
                    A[i * (d + 1) + j] = points[p * (d + 1) + j];
        }
    }

    CH_FLOAT* meanp, * absdist, * reldist, * desReldist;
    meanp = (CH_FLOAT*)calloc(d, sizeof(CH_FLOAT));
    for (i = d + 1; i < nVert; i++)
        for (j = 0; j < d; j++)
            meanp[j] += points[i * (d + 1) + j];
    for (j = 0; j < d; j++) meanp[j] = meanp[j] / (CH_FLOAT)(nVert - d - 1);

    absdist = (CH_FLOAT*)malloc((nVert - d - 1) * d * sizeof(CH_FLOAT));
    for (i = d + 1, k = 0; i < nVert; i++, k++)
        for (j = 0; j < d; j++)
            absdist[k * d + j] = (points[i * (d + 1) + j] - meanp[j]) / span[j];

    reldist = (CH_FLOAT*)calloc((nVert - d - 1), sizeof(CH_FLOAT));
    desReldist = (CH_FLOAT*)malloc((nVert - d - 1) * sizeof(CH_FLOAT));
    for (i = 0; i < (nVert - d - 1); i++)
        for (j = 0; j < d; j++)
            reldist[i] += pow(absdist[i * d + j], 2.0);

    int num_pleft, cnt;
    int* ind, * pleft;
    ind = (int*)malloc((nVert - d - 1) * sizeof(int));
    pleft = (int*)malloc((nVert - d - 1) * sizeof(int));
    sort_float(reldist, desReldist, ind, (nVert - d - 1), 1);

    num_pleft = (nVert - d - 1);
    for (i = 0; i < num_pleft; i++) pleft[i] = ind[i] + d + 1;

    memset(A, 0, (d + 1) * (d + 1) * sizeof(CH_FLOAT));

    cnt = 0;

    CH_FLOAT detA;
    CH_FLOAT* points_cf, * points_s;
    int* visible_ind, * visible, * nonvisible_faces, * f0, * face_s, * u, * gVec, * horizon, * hVec, * pp, * hVec_mem_face;
    int num_visible_ind, num_nonvisible_faces, n_newfaces, count, vis;
    int f0_sum, u_len, start, num_p, index, horizon_size1;
    int FUCKED;
    FUCKED = 0;
    u = horizon = NULL;
    nFaces = d + 1;
    visible_ind = (int*)malloc(nFaces * sizeof(int));
    points_cf = (CH_FLOAT*)malloc(nFaces * sizeof(CH_FLOAT));
    points_s = (CH_FLOAT*)malloc(d * sizeof(CH_FLOAT));
    face_s = (int*)malloc(d * sizeof(int));
    gVec = (int*)malloc(d * sizeof(int));
    while ((num_pleft > 0)) {
        i = pleft[0];

        for (j = 0; j < num_pleft - 1; j++) pleft[j] = pleft[j + 1];
        num_pleft--;
        if (num_pleft == 0) free(pleft);
        else pleft = (int*)realloc(pleft, num_pleft * sizeof(int));

        cnt++;

        for (j = 0; j < d; j++) points_s[j] = points[i * (d + 1) + j];
        points_cf = (CH_FLOAT*)realloc(points_cf, nFaces * sizeof(CH_FLOAT));
        visible_ind = (int*)realloc(visible_ind, nFaces * sizeof(int));
        for (j = 0; j < nFaces; j++) {
            points_cf[j] = 0;
            for (k = 0; k < d; k++) points_cf[j] += points_s[k] * cf[j * d + k];
        }
        num_visible_ind = 0;
        for (j = 0; j < nFaces; j++) {
            if (points_cf[j] + df[j] > 0.0) {
                num_visible_ind++;
                visible_ind[j] = 1;
            } else visible_ind[j] = 0;
        }
        num_nonvisible_faces = nFaces - num_visible_ind;

        if (num_visible_ind != 0) {
            visible = (int*)malloc(num_visible_ind * sizeof(int));
            for (j = 0, k = 0; j < nFaces; j++) {
                if (visible_ind[j] == 1) {
                    visible[k] = j;
                    k++;
                }
            }

            nonvisible_faces = (int*)malloc(num_nonvisible_faces * d * sizeof(int));
            f0 = (int*)malloc(num_nonvisible_faces * d * sizeof(int));
            for (j = 0, k = 0; j < nFaces; j++) {
                if (visible_ind[j] == 0) {
                    for (l = 0; l < d; l++) nonvisible_faces[k * d + l] = faces[j * d + l];
                    k++;
                }
            }

            count = 0;
            for (j = 0; j < num_visible_ind; j++) {
                vis = visible[j];
                for (k = 0; k < d; k++) face_s[k] = faces[vis * d + k];
                sort_int(face_s, NULL, NULL, d, 0);
                ismember(nonvisible_faces, face_s, f0, num_nonvisible_faces * d, d);
                u_len = 0;

                for (k = 0; k < num_nonvisible_faces; k++) {
                    f0_sum = 0;
                    for (l = 0; l < d; l++) f0_sum += f0[k * d + l];
                    if (f0_sum == d - 1) {
                        u_len++;
                        if (u_len == 1) u = (int*)malloc(u_len * sizeof(int));
                        else u = (int*)realloc(u, u_len * sizeof(int));
                        u[u_len - 1] = k;
                    }
                }
                for (k = 0; k < u_len; k++) {
                    count++;
                    if (count == 1) horizon = (int*)malloc(count * (d - 1) * sizeof(int));
                    else horizon = (int*)realloc(horizon, count * (d - 1) * sizeof(int));
                    for (l = 0; l < d; l++) gVec[l] = nonvisible_faces[u[k] * d + l];
                    for (l = 0, h = 0; l < d; l++) {
                        if (f0[u[k] * d + l]) {
                            horizon[(count - 1) * (d - 1) + h] = gVec[l];
                            h++;
                        }
                    }
                }
                if (u_len != 0) free(u);
            }
            horizon_size1 = count;
            for (j = 0, l = 0; j < nFaces; j++) {
                if (!visible_ind[j]) {
                    for (k = 0; k < d; k++) faces[l * d + k] = faces[j * d + k];

                    for (k = 0; k < d; k++) cf[l * d + k] = cf[j * d + k];
                    df[l] = df[j];
                    l++;
                }
            }

            nFaces = nFaces - num_visible_ind;
            faces = (int*)realloc(faces, nFaces * d * sizeof(int));
            cf = (CH_FLOAT*)realloc(cf, nFaces * d * sizeof(CH_FLOAT));
            df = (CH_FLOAT*)realloc(df, nFaces * sizeof(CH_FLOAT));

            start = nFaces;

            n_newfaces = horizon_size1;
            for (j = 0; j < n_newfaces; j++) {
                nFaces++;
                faces = (int*)realloc(faces, nFaces * d * sizeof(int));
                cf = (CH_FLOAT*)realloc(cf, nFaces * d * sizeof(CH_FLOAT));
                df = (CH_FLOAT*)realloc(df, nFaces * sizeof(CH_FLOAT));
                for (k = 0; k < d - 1; k++) faces[(nFaces - 1) * d + k] = horizon[j * (d - 1) + k];
                faces[(nFaces - 1) * d + (d - 1)] = i;

                for (k = 0; k < d; k++)
                    for (l = 0; l < d; l++)
                        p_s[k * d + l] = points[(faces[(nFaces - 1) * d + k]) * (d + 1) + l];
                plane_3d(p_s, cfi, &dfi);
                for (k = 0; k < d; k++) cf[(nFaces - 1) * d + k] = cfi[k];
                df[(nFaces - 1)] = dfi;
                if (nFaces > CH_MAX_NUM_FACES) {
                    FUCKED = 1;
                    nFaces = 0;
                    break;
                }
            }

            hVec = (int*)malloc(nFaces * sizeof(int));
            hVec_mem_face = (int*)malloc(nFaces * sizeof(int));
            for (j = 0; j < nFaces; j++) hVec[j] = j;
            for (k = start; k < nFaces; k++) {
                for (j = 0; j < d; j++) face_s[j] = faces[k * d + j];
                sort_int(face_s, NULL, NULL, d, 0);
                ismember(hVec, face_s, hVec_mem_face, nFaces, d);
                num_p = 0;
                for (j = 0; j < nFaces; j++) if (!hVec_mem_face[j]) num_p++;
                pp = (int*)malloc(num_p * sizeof(int));
                for (j = 0, l = 0; j < nFaces; j++) {
                    if (!hVec_mem_face[j]) {
                        pp[l] = hVec[j];
                        l++;
                    }
                }
                index = 0;
                detA = 0.0;

                while (detA == 0.0) {
                    for (j = 0; j < d; j++)
                        for (l = 0; l < d + 1; l++)
                            A[j * (d + 1) + l] = points[(faces[k * d + j]) * (d + 1) + l];
                    for (; j < d + 1; j++)
                        for (l = 0; l < d + 1; l++)
                            A[j * (d + 1) + l] = points[pp[index] * (d + 1) + l];
                    index++;
                    detA = det_4x4(A);
                }

                if (detA < 0.0) {
                    for (j = 0; j < d; j++) face_tmp[j] = faces[k * d + j];
                    for (j = 0, l = d - 2; j < d - 1; j++, l++)
                        faces[k * d + l] = face_tmp[d - j - 1];

                    for (j = 0; j < d; j++) cf[k * d + j] = -cf[k * d + j];
                    df[k] = -df[k];
                    for (l = 0; l < d; l++)
                        for (j = 0; j < d + 1; j++)
                            A[l * (d + 1) + j] = points[(faces[k * d + l]) * (d + 1) + j];
                    for (; l < d + 1; l++)
                        for (j = 0; j < d + 1; j++)
                            A[l * (d + 1) + j] = points[pp[index] * (d + 1) + j];
                }
                free(pp);
            }
            free(horizon);
            free(f0);
            free(nonvisible_faces);
            free(visible);
            free(hVec);
            free(hVec_mem_face);
        }
        if (FUCKED) break;
    }

    if (FUCKED) {
        (*out_faces) = NULL;
        (*nOut_faces) = 0;
    } else {
        (*out_faces) = (int*)malloc(nFaces * d * sizeof(int));
        memcpy((*out_faces), faces, nFaces * d * sizeof(int));
        (*nOut_faces) = nFaces;
    }

    free(visible_ind);
    free(points_cf);
    free(points_s);
    free(face_s);
    free(gVec);
    free(meanp);
    free(absdist);
    free(reldist);
    free(desReldist);
    free(ind);
    free(span);
    free(points);
    free(faces);
    free(aVec);
    free(cf);
    free(cfi);
    free(df);
    free(p_s);
    free(face_tmp);
    free(fVec);
    free(asfVec);
    free(bVec);
    free(A);
}

void convhull_3d_export_obj(
    ch_vertex* const vertices,
    const int nVert,
    int* const faces,
    const int nFaces,
    const int keepOnlyUsedVerticesFLAG,
    char* const obj_filename)
{
    int i, j;
    char path[256] = "\0";
    CV_STRNCPY(path, obj_filename, strlen(obj_filename));
    FILE* obj_file;
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
    CV_STRCAT(path, ".obj");
    fopen_s(&obj_file, path, "wt");
#else
    obj_file = fopen(strcat(path, ".obj"), "wt");
#endif
    fprintf(obj_file, "o\n");
    CH_FLOAT scale;
    ch_vec3 v1, v2, normal;

    if (keepOnlyUsedVerticesFLAG) {
        for (i = 0; i < nFaces; i++)
            for (j = 0; j < 3; j++)
                fprintf(obj_file, "v %f %f %f\n", vertices[faces[i * 3 + j]].x,
                    vertices[faces[i * 3 + j]].y, vertices[faces[i * 3 + j]].z);
    } else {
        for (i = 0; i < nVert; i++)
            fprintf(obj_file, "v %f %f %f\n", vertices[i].x,
                vertices[i].y, vertices[i].z);
    }

    for (i = 0; i < nFaces; i++) {
        v1 = vertices[faces[i * 3 + 1]];
        v2 = vertices[faces[i * 3 + 2]];
        v1.x -= vertices[faces[i * 3]].x;
        v1.y -= vertices[faces[i * 3]].y;
        v1.z -= vertices[faces[i * 3]].z;
        v2.x -= vertices[faces[i * 3]].x;
        v2.y -= vertices[faces[i * 3]].y;
        v2.z -= vertices[faces[i * 3]].z;
        normal = cross(&v1, &v2);

        scale = 1.0 / (sqrt(pow(normal.x, 2.0) + pow(normal.y, 2.0) + pow(normal.z, 2.0)) + 2.23e-9);
        normal.x *= scale;
        normal.y *= scale;
        normal.z *= scale;
        fprintf(obj_file, "vn %f %f %f\n", normal.x, normal.y, normal.z);
    }

    if (keepOnlyUsedVerticesFLAG) {
        for (i = 0; i < nFaces; i++) {
            fprintf(obj_file, "f %u//%u %u//%u %u//%u\n",
                i * 3 + 1, i + 1,
                i * 3 + 1 + 1, i + 1,
                i * 3 + 2 + 1, i + 1);
        }
    } else {
        for (i = 0; i < nFaces; i++) {
            fprintf(obj_file, "f %u//%u %u//%u %u//%u\n",
                faces[i * 3] + 1, i + 1,
                faces[i * 3 + 1] + 1, i + 1,
                faces[i * 3 + 2] + 1, i + 1);
        }
    }
    fclose(obj_file);
}

void convhull_3d_export_m(
    ch_vertex* const vertices,
    const int nVert,
    int* const faces,
    const int nFaces,
    char* const m_filename)
{
    int i;
    char path[256] = { "\0" };
    memcpy(path, m_filename, strlen(m_filename));
    FILE* m_file;
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
    CV_STRCAT(path, ".m");
    fopen_s(&m_file, path, "wt");
#else
    m_file = fopen(strcat(path, ".m"), "wt");
#endif

    fprintf(m_file, "vertices = [\n");
    for (i = 0; i < nVert; i++)
        fprintf(m_file, "%f, %f, %f;\n", vertices[i].x, vertices[i].y, vertices[i].z);
    fprintf(m_file, "];\n\n\n");
    fprintf(m_file, "faces = [\n");
    for (int i = 0; i < nFaces; i++) {
        fprintf(m_file, " %u, %u, %u;\n",
            faces[3 * i + 0] + 1,
            faces[3 * i + 1] + 1,
            faces[3 * i + 2] + 1);
    }
    fprintf(m_file, "];\n\n\n");
    fclose(m_file);
}

void extractVerticesFromObjFile(char* const obj_filename, ch_vertex** out_vertices, int* out_nVert) {
    FILE* obj_file;
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
    CV_STRCAT(obj_filename, ".obj");
    fopen_s(&obj_file, obj_filename, "r");
#else
    obj_file = fopen(strcat(obj_filename, ".obj"), "r");
#endif

    unsigned int nVert = 0;
    char line[256];
    while (fgets(line, sizeof(line), obj_file)) {
        char* vexists = strstr(line, "v ");
        if (vexists != NULL) nVert++;
    }
    (*out_nVert) = nVert;
    (*out_vertices) = (ch_vertex*)malloc(nVert * sizeof(ch_vertex));

    rewind(obj_file);
    int i = 0;
    int vertID, prev_char_isDigit, current_char_isDigit;
    char vert_char[256] = { 0 };
    while (fgets(line, sizeof(line), obj_file)) {
        char* vexists = strstr(line, "v ");
        if (vexists != NULL) {
            prev_char_isDigit = 0;
            vertID = -1;
            for (int j = 0; j < (int)strlen(line) - 1; j++) {
                if (isdigit(line[j]) || line[j] == '.' || line[j] == '-' || line[j] == '+' || line[j] == 'E' || line[j] == 'e') {
                    vert_char[strlen(vert_char)] = line[j];
                    current_char_isDigit = 1;
                } else current_char_isDigit = 0;
                if ((prev_char_isDigit && !current_char_isDigit) || j == (int)strlen(line) - 2) {
                    vertID++;
                    if (vertID > 4) {
                        free((*out_vertices));
                        (*out_vertices) = NULL;
                        (*out_nVert) = 0;
                        return;
                    }
                    (*out_vertices)[i].v[vertID] = atof(vert_char);
                    memset(vert_char, 0, 256 * sizeof(char));
                }
                prev_char_isDigit = current_char_isDigit;
            }
            i++;
        }
    }
}

#endif /* CONVHULL_3D_ENABLE */
