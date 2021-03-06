/* 3D space map output */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include "config.h"
#include "sfile.h"
#include "sycamera.h"
#include "sycout.h"
#include "util.h"

#ifdef USE_MPI
#	include <mpi.h>
#	include "smpi.h"
#endif

space3d_real_t *space3d_mainmap, *space3d_map;
space3d_pixels_t *space3d_mainimage, *space3d_image;
int space3d_allocated,		/* Number of allocated pixels in 'space3d_map_X' */
	space3d_pixels=0;		/* Number of pixels (if applicable) */
enum space3d_model_type space3d_type;
char *space3d_output;
double *space3d_point0, *space3d_point1;

#pragma omp threadprivate(space3d_map,space3d_image,space3d_allocated)

void sycout_space3d_deinit_run(void) {
	#pragma omp critical
	{
		if (space3d_type == SPACE3D_MT_REAL) {
			/* Extend 'space3d_mainmap' */
			space3d_mainmap->x = realloc(space3d_mainmap->x, sizeof(double)*(space3d_mainmap->n+space3d_map->n));
			space3d_mainmap->y = realloc(space3d_mainmap->y, sizeof(double)*(space3d_mainmap->n+space3d_map->n));
			space3d_mainmap->z = realloc(space3d_mainmap->z, sizeof(double)*(space3d_mainmap->n+space3d_map->n));
			space3d_mainmap->intensity = realloc(space3d_mainmap->intensity, sizeof(double)*(space3d_mainmap->n+space3d_map->n));

			/* Copy over map generated by this thread */
			memcpy(space3d_mainmap->x+space3d_mainmap->n, space3d_map->x, sizeof(double)*space3d_map->n);
			memcpy(space3d_mainmap->y+space3d_mainmap->n, space3d_map->y, sizeof(double)*space3d_map->n);
			memcpy(space3d_mainmap->z+space3d_mainmap->n, space3d_map->z, sizeof(double)*space3d_map->n);
			memcpy(space3d_mainmap->intensity+space3d_mainmap->n, space3d_map->intensity, sizeof(double)*space3d_map->n);

			space3d_mainmap->n += space3d_map->n;
		} else {
			/* Add images */
			size_t i, imgsize = space3d_image->pixels*space3d_image->pixels*space3d_image->pixels;
			for (i = 0; i < imgsize; i++)
				space3d_mainimage->image[i] += space3d_image->image[i];

			free(space3d_image->image);
			free(space3d_image);
		}
	}
}
void sycout_space3d_init(struct general_settings *set) {
	int i;
	space3d_type = SPACE3D_MT_PIXELS;
	space3d_output = NULL;
	space3d_pixels = 0;

	for (i = 0; i < set->n; i++) {
		if (!strcmp(set->setting[i], "point0"))
			space3d_point0 = atodpn(set->value[i], 3, NULL);
		else if (!strcmp(set->setting[i], "point1"))
			space3d_point1 = atodpn(set->value[i], 3, NULL);
		else if (!strcmp(set->setting[i], "output"))
			space3d_output = set->value[i];
		else if (!strcmp(set->setting[i], "pixels"))
			space3d_pixels = atoi(set->value[i]);
		else if (!strcmp(set->setting[i], "type")) {
			if (!strcmp(set->value[i], "pixels"))
				space3d_type = SPACE3D_MT_PIXELS;
			else if (!strcmp(set->value[i], "real"))
				space3d_type = SPACE3D_MT_REAL;
			else {
				fprintf(stderr, "sycout space3d: Unrecognized model type: '%s'!\n", set->value[i]);
				exit(-1);
			}
		} else {
			fprintf(stderr, "sycout space3d: Unrecognized setting: '%s'!\n", set->setting[i]);
			exit(-1);
		}
	}

	if (space3d_output == NULL) {
		fprintf(stderr, "sycout space3d: No output filename specified!\n");
		exit(-1);
	}

	if (space3d_type == SPACE3D_MT_PIXELS) {
		if (space3d_pixels <= 0) {
			fprintf(stderr, "sycout space3d: The number of pixels must be set, and must be a positive integer!\n");
			exit(-1);
		}
	}

	if (space3d_type == SPACE3D_MT_REAL) {
		space3d_mainmap = malloc(sizeof(space3d_real_t));
		space3d_mainmap->n = 0;
		space3d_mainmap->x = NULL;
		space3d_mainmap->y = NULL;
		space3d_mainmap->z = NULL;
		space3d_mainmap->intensity = NULL;
	} else {
		space3d_mainimage = malloc(sizeof(space3d_pixels_t));
		space3d_mainimage->pixels = space3d_pixels;
		space3d_mainimage->xmin = space3d_point0[0];
		space3d_mainimage->xmax = space3d_point1[0];
		space3d_mainimage->ymin = space3d_point0[1];
		space3d_mainimage->ymax = space3d_point1[1];
		space3d_mainimage->zmin = space3d_point0[2];
		space3d_mainimage->zmax = space3d_point1[2];

		/* Calculate size per image and tell user */
		size_t memsize = space3d_pixels*
						 space3d_pixels*
						 space3d_pixels*
						 sizeof(double);
		double rsize = memsize;
		char suffix[] = " kMGTP";
		i = 0;
		while (rsize > 1e3) {rsize/=1e3; i++;}
		printf("The 3D map will require %.2f %cB of memory per thread (+ one common)\n", rsize, suffix[i]);

		/* Allocate memory for common image */
		space3d_mainimage->image = malloc(memsize);
	}
}
/**
 * Allocate memory for map/image
 **/
void sycout_space3d_init_run(void) {
	if (space3d_type == SPACE3D_MT_REAL) {
		space3d_map = malloc(sizeof(space3d_real_t));
		space3d_map->n = 0;
		space3d_map->x = NULL;
		space3d_map->y = NULL;
		space3d_map->z = NULL;
		space3d_map->intensity = NULL;

		space3d_allocated = 0;
	} else {
		space3d_image = malloc(sizeof(space3d_pixels_t));
		space3d_image->pixels = space3d_mainimage->pixels;
		space3d_image->xmin = space3d_mainimage->xmin;
		space3d_image->xmax = space3d_mainimage->xmax;
		space3d_image->ymin = space3d_mainimage->ymin;
		space3d_image->ymax = space3d_mainimage->ymax;
		space3d_image->zmin = space3d_mainimage->zmin;
		space3d_image->zmax = space3d_mainimage->zmax;

		size_t memsize = space3d_image->pixels*
						 space3d_image->pixels*
						 space3d_image->pixels*
						 sizeof(double);
		space3d_image->image = malloc(memsize);
	}
}
void sycout_space3d_init_particle(particle *p) {}
void sycout_space3d_step(struct sycout_data *sd) {
	if (space3d_type == SPACE3D_MT_REAL) {
		if (space3d_allocated <= space3d_map->n+1) {
			space3d_allocated += SPACE3D_CHUNKSIZE;
			space3d_map->x = realloc(space3d_map->x, sizeof(double)*space3d_allocated);
			space3d_map->y = realloc(space3d_map->y, sizeof(double)*space3d_allocated);
			space3d_map->z = realloc(space3d_map->z, sizeof(double)*space3d_allocated);
			space3d_map->intensity = realloc(space3d_map->intensity, sizeof(double)*space3d_allocated);
		}

		int i = space3d_map->n++;
		space3d_map->x[i] = sd->sd->x;
		space3d_map->y[i] = sd->sd->y;
		space3d_map->z[i] = sd->sd->z;
		space3d_map->intensity[i] = sd->brightness * sd->differential;
	} else {
		long long int I = (long long int)(((sd->sd->x-space3d_image->xmin)/(space3d_image->xmax-space3d_image->xmin)) * space3d_pixels),
				      J = (long long int)(((sd->sd->y-space3d_image->ymin)/(space3d_image->ymax-space3d_image->ymin)) * space3d_pixels),
				      K = (long long int)(((sd->sd->z-space3d_image->zmin)/(space3d_image->zmax-space3d_image->zmin)) * space3d_pixels);

		/* Is pixel within image? */
		if (I < 0 || I >= space3d_pixels ||
			J < 0 || J >= space3d_pixels ||
			K < 0 || K >= space3d_pixels)
			return;	/* ...no */

		size_t index = I*space3d_pixels*space3d_pixels + J*space3d_pixels + K;
		space3d_image->image[index] += sd->brightness * sd->differential;
	}
}
void sycout_space3d_write(int mpi_rank, int nprocesses) {
	sFILE *sf;
	enum sfile_type ftype;

#ifdef USE_MPI
	printf("[%d] (sycout space3d) Waiting for 'output ready' from previous process.\n", mpi_rank);
	smpi_wor(SYCOUT_MPIID_SPACE3D);
	printf("[%d] (sycout space3d) Received 'output ready' signal from previous process.\n", mpi_rank);
#endif

	ftype = sfile_get_filetype(space3d_output);
	if (ftype == FILETYPE_UNKNOWN) {
		ftype = FILETYPE_SDT;
		if (mpi_rank == 0)
			printf("WARNING: (sycout space3d): Unable to determine filetype of output. Defaulting to SDT.\n");
	}

	sf = sfile_init(ftype);

	/* TODO: If not the root process, read in
	 * image and combine to current process's. */
	if (mpi_rank > 0) {
		//f = fopen(space3d_output, "a");
		fprintf(stderr, "ERROR: space3d output can not be saved when using MPI.\n");
		exit(EXIT_FAILURE);
	} else {
		//f = fopen(space3d_output, "w");
		if (!sf->open(sf, space3d_output, SFILE_MODE_WRITE)) {
			fprintf(stderr, "ERROR: Unable to open file for writing: '%s'.\n", space3d_output);
			exit(EXIT_FAILURE);
		}
	}

	fprintf(stdout, "[%d] - Writing Space3D map list\n", mpi_rank);

	/* Write data */
	if (space3d_type == SPACE3D_MT_REAL) {
		sf->write_list(sf, "x", space3d_mainmap->x, space3d_mainmap->n);
		sf->write_list(sf, "y", space3d_mainmap->y, space3d_mainmap->n);
		sf->write_list(sf, "z", space3d_mainmap->z, space3d_mainmap->n);
		sf->write_list(sf, "intensity", space3d_mainmap->intensity, space3d_mainmap->n);
	} else {
		size_t p = space3d_mainimage->pixels; p = p*p*p;
		double pixls = space3d_mainimage->pixels;
		sf->write_list(sf, "pixels", &pixls, 1);
		sf->write_list(sf, "image", space3d_mainimage->image, p);
		sf->write_list(sf, "xmin", &space3d_mainimage->xmin, 1);
		sf->write_list(sf, "xmax", &space3d_mainimage->xmax, 1);
		sf->write_list(sf, "ymin", &space3d_mainimage->ymin, 1);
		sf->write_list(sf, "ymax", &space3d_mainimage->ymax, 1);
		sf->write_list(sf, "zmin", &space3d_mainimage->zmin, 1);
		sf->write_list(sf, "zmax", &space3d_mainimage->zmax, 1);
	}

	sf->close(sf);

#ifdef USE_MPI
    printf("[%d] (sycout space3d) Done, sending 'output ready' to next process.\n", mpi_rank);
    smpi_sor(SYCOUT_MPIID_SPACE3D);

	if (mpi_rank == nprocesses-1) {
#endif
		fprintf(stdout, "[%d] - Wrote output to '%s'\n", mpi_rank, space3d_output);
#ifdef USE_MPI
	}
#endif
}

