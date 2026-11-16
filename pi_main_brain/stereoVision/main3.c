/*
 * stereo.c - block-matching stereo depthmap using libpng
 *
 * Build:  gcc -O2 -o stereo main.c -lpng -lm
 * Usage:  ./stereo -b BASELINE -f FOV [-x CX] [-y CY] \
 *                  left.png right.png out.png n [max_disparity] [window_radius]
 *
 *   -b, --baseline  distance between the two camera centres, in meters
 *   -f, --fov       horizontal field of view in degrees (same for both cameras).
 *                   Focal length in pixels is inferred as (width/2)/tan(fov/2),
 *                   assuming square pixels and the same focal length in x and y.
 *   -x, --cx        principal point x in pixels (default: (width-1)/2)
 *   -y, --cy        principal point y in pixels (default: (height-1)/2)
 *
 * Inputs must be a rectified stereo pair of identical size, with identical
 * intrinsics. Output PNG brightness is proportional to disparity (brighter =
 * closer). An n x n matrix of distances in meters (Euclidean distance from the
 * left camera's optical centre to the matched 3D point) is printed to stdout;
 * pixels with zero disparity are at infinity and print as "inf".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <png.h>

typedef struct {
	int w, h;
	unsigned char *px;   /* w*h, 8-bit grayscale */
} Image;

static int read_png(const char *path, Image *img)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) { perror(path); return -1; }

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png_create_info_struct(png);
	img->px = NULL;

	if (setjmp(png_jmpbuf(png))) {
		fprintf(stderr, "%s: libpng read error\n", path);
		png_destroy_read_struct(&png, &info, NULL);
		free(img->px);
		img->px = NULL;
		fclose(fp);
		return -1;
	}

	png_init_io(png, fp);
	png_read_info(png, info);

	img->w = (int)png_get_image_width(png, info);
	img->h = (int)png_get_image_height(png, info);
	int color = png_get_color_type(png, info);
	int depth = png_get_bit_depth(png, info);

	/* Normalise whatever we were given to 8-bit grayscale. */
	if (depth == 16) png_set_strip_16(png);
	if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
	if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
	if (color & PNG_COLOR_MASK_COLOR) png_set_rgb_to_gray(png, 1, -1, -1);
	if (color & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);
	png_read_update_info(png, info);

	img->px = malloc((size_t)img->w * img->h);
	png_bytep *rows = malloc(sizeof(png_bytep) * img->h);
	if (!img->px || !rows) png_error(png, "out of memory");
	for (int y = 0; y < img->h; y++)
		rows[y] = img->px + (size_t)y * img->w;

	png_read_image(png, rows);
	free(rows);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	return 0;
}

static int write_png(const char *path, const Image *img)
{
	FILE *fp = fopen(path, "wb");
	if (!fp) { perror(path); return -1; }

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png_create_info_struct(png);

	if (setjmp(png_jmpbuf(png))) {
		fprintf(stderr, "%s: libpng write error\n", path);
		png_destroy_write_struct(&png, &info);
		fclose(fp);
		return -1;
	}

	png_init_io(png, fp);
	png_set_IHDR(png, info, img->w, img->h, 8, PNG_COLOR_TYPE_GRAY,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
			PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);

	for (int y = 0; y < img->h; y++)
		png_write_row(png, img->px + (size_t)y * img->w);

	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	fclose(fp);
	return 0;
}

static inline int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline int at(const Image *im, int x, int y)
{
	return im->px[(size_t)clampi(y, 0, im->h - 1) * im->w + clampi(x, 0, im->w - 1)];
}

/* For each left pixel, find the horizontal shift d in [0, max_d] that minimises
 * the sum of absolute differences over a (2r+1)x(2r+1) window. */
static void compute_disparity(const Image *L, const Image *R, Image *out,
		int *disp, int max_d, int r)
{
	for (int y = 0; y < L->h; y++) {
		for (int x = 0; x < L->w; x++) {
			int best_d = 0;
			long best_cost = -1;

			for (int d = 0; d <= max_d && x - d >= 0; d++) {
				long cost = 0;
				for (int j = -r; j <= r; j++)
					for (int i = -r; i <= r; i++) {
						int diff = at(L, x + i, y + j) - at(R, x + i - d, y + j);
						cost += diff < 0 ? -diff : diff;
					}
				if (best_cost < 0 || cost < best_cost) {
					best_cost = cost;
					best_d = d;
				}
			}
			disp[(size_t)y * L->w + x] = best_d;
			out->px[(size_t)y * L->w + x] = (unsigned char)(best_d * 255 / max_d);
		}
	}
}

/* Euclidean distance (meters) from the left camera centre to the 3D point
 * seen at pixel (x, y) with disparity d (pixels), for a rectified pair:
 *   Z = f*B/d,  X = (x-cx)*Z/f,  Y = (y-cy)*Z/f,  dist = sqrt(X^2+Y^2+Z^2)
 * d <= 0 means the point is at infinity (or unmatched). */
static double distance_m(int d, int x, int y,
		double baseline, double focal, double cx, double cy)
{
	if (d <= 0) return INFINITY;
	double Z = focal * baseline / d;
	double X = (x - cx) * Z / focal;
	double Y = (y - cy) * Z / focal;
	return sqrt(X * X + Y * Y + Z * Z);
}

static int parse_double(const char *s, const char *name, double *out)
{
	char *end;
	*out = strtod(s, &end);
	if (end == s || *end != '\0' || !isfinite(*out)) {
		fprintf(stderr, "invalid value for %s: '%s'\n", name, s);
		return -1;
	}
	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr,
			"usage: %s -b BASELINE_M -f HFOV_DEG [-x CX_PX] [-y CY_PX] "
			"left.png right.png out.png n [max_disparity=64] [window_radius=3]\n",
			prog);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{"baseline", required_argument, NULL, 'b'},
		{"fov",      required_argument, NULL, 'f'},
		{"cx",       required_argument, NULL, 'x'},
		{"cy",       required_argument, NULL, 'y'},
		{NULL, 0, NULL, 0}
	};
	double baseline = 0, fov = 0, cx = NAN, cy = NAN;
	int have_b = 0, have_f = 0, c;

	while ((c = getopt_long(argc, argv, "b:f:x:y:", opts, NULL)) != -1) {
		switch (c) {
			case 'b': if (parse_double(optarg, "baseline", &baseline)) return 1; have_b = 1; break;
			case 'f': if (parse_double(optarg, "fov", &fov)) return 1; have_f = 1; break;
			case 'x': if (parse_double(optarg, "cx", &cx)) return 1; break;
			case 'y': if (parse_double(optarg, "cy", &cy)) return 1; break;
			default:  usage(argv[0]); return 1;
		}
	}

	char **pos = argv + optind;
	int npos = argc - optind;
	if (npos < 4 || !have_b || !have_f) { usage(argv[0]); return 1; }
	if (baseline <= 0 || fov <= 0 || fov >= 180) {
		fprintf(stderr, "baseline must be positive and fov must be in (0, 180) degrees\n");
		return 1;
	}

	int n     = atoi(pos[3]);
	int max_d = npos > 4 ? atoi(pos[4]) : 64;
	int r     = npos > 5 ? atoi(pos[5]) : 3;
	if (max_d < 1 || r < 0 || n < 1) { fprintf(stderr, "bad parameters\n"); return 1; }

	Image L, R, D;
	if (read_png(pos[0], &L) || read_png(pos[1], &R)) return 1;
	if (L.w != R.w || L.h != R.h) {
		fprintf(stderr, "image sizes differ (%dx%d vs %dx%d)\n", L.w, L.h, R.w, R.h);
		return 1;
	}

	/* Infer focal length (pixels) from horizontal FOV and image width. */
	double focal = (L.w / 2.0) / tan(fov * M_PI / 360.0);

	/* Default principal point: image centre, in pixel-index coordinates. */
	if (isnan(cx)) cx = (L.w - 1) / 2.0;
	if (isnan(cy)) cy = (L.h - 1) / 2.0;

	D.w = L.w;
	D.h = L.h;
	D.px = calloc((size_t)D.w * D.h, 1);
	int *disp = calloc((size_t)L.w * L.h, sizeof(int));
	if (!D.px || !disp) { fprintf(stderr, "out of memory\n"); return 1; }

	compute_disparity(&L, &R, &D, disp, max_d, r);

	int rc = write_png(pos[2], &D);

	/* Print an n x n matrix of distances (meters) sampled at the centre of
	 * each output bin. */
	printf("distance matrix %dx%d (meters from left camera centre; inf = no disparity):\n", n, n);
	for (int iy = 0; iy < n; iy++) {
		int y = ((2 * iy + 1) * L.h) / (2 * n);
		for (int ix = 0; ix < n; ix++) {
			int x = ((2 * ix + 1) * L.w) / (2 * n);
			int d = disp[(size_t)y * L.w + x];
			printf("%9.3f", distance_m(d, x, y, baseline, focal, cx, cy));
			if (ix + 1 < n) putchar(' ');
		}
		putchar('\n');
	}

	free(L.px); free(R.px); free(D.px); free(disp);
	return rc ? 1 : 0;
}
