/*
 * Copyright (C) 2008 Till Harbaum <till@harbaum.org>.
 *
 * This file is part of OSM2Go.
 *
 * OSM2Go is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OSM2Go is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OSM2Go.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gps.h"

#include "appdata.h"
#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef ENABLE_GPSBT
#include <gpsbt.h>
#include <gpsmgr.h>
#include <errno.h>
#endif

/* maybe user configurable later on ... */
#define GPSD_HOST "127.0.0.1"
#define GPSD_PORT 2947

gboolean gps_get_pos(appdata_t *appdata, pos_t *pos, float *alt) {
  pos_t tmp;
  if(!pos) pos = &tmp;
  pos->lat = NAN;

  g_mutex_lock(appdata->gps_state->mutex);
  if(appdata->gps_state->gpsdata.set & STATUS_SET) {
    if(appdata->gps_state->gpsdata.status != STATUS_NO_FIX) {
      if(appdata->gps_state->gpsdata.set & LATLON_SET)
	*pos = appdata->gps_state->gpsdata.fix.pos;
      if(alt && appdata->gps_state->gpsdata.set & ALTITUDE_SET)
	*alt = appdata->gps_state->gpsdata.fix.alt;
    }
  }

  g_mutex_unlock(appdata->gps_state->mutex);

  return(!isnan(pos->lat));
}

static int gps_connect(gps_state_t *gps_state) {
  GnomeVFSResult vfs_result;
#ifdef ENABLE_GPSBT
  char errstr[256] = "";

  /* We need to start gpsd (via gpsbt) first. */
  memset(&gps_state->context, 0, sizeof(gps_state->context));
  errno = 0;

  if(gpsbt_start(NULL, 0, 0, 0, errstr, sizeof(errstr),
		 0, &gps_state->context) < 0) {
    printf("Error connecting to GPS receiver: (%d) %s (%s)\n",
	   errno, strerror(errno), errstr);
  }
#endif

  /************** from here down pure gnome/gtk/gpsd ********************/

  /* try to connect to gpsd */
  /* Create a socket to interact with GPSD. */

  printf("GPSD: trying to connect to %s %d\n", GPSD_HOST, GPSD_PORT);

  int retries = 5;
  while(retries &&
	(GNOME_VFS_OK != (vfs_result = gnome_vfs_inet_connection_create(
		&gps_state->iconn, GPSD_HOST, GPSD_PORT, NULL)))) {
    printf("Error creating connection to GPSD, retrying ...\n");

    retries--;
    sleep(1);
  }

  if(!retries) {
    printf("Finally failed ...\n");
    return -1;
  }

  retries = 5;
  while(retries && ((gps_state->socket =
     gnome_vfs_inet_connection_to_socket(gps_state->iconn)) == NULL)) {
    printf("Error creating connecting GPSD socket, retrying ...\n");

    retries--;
    sleep(1);
  }

  if(!retries) {
    printf("Finally failed ...\n");
    gnome_vfs_inet_connection_destroy(gps_state->iconn, NULL);
    return -1;
  }

  GTimeVal timeout = { 10, 0 };
  if(GNOME_VFS_OK != (vfs_result = gnome_vfs_socket_set_timeout(
	gps_state->socket, &timeout, NULL))) {
    printf("Error setting GPSD timeout\n");
    gnome_vfs_inet_connection_destroy(gps_state->iconn, NULL);
    return -1;
  }

  printf("GPSD connected ...\n");

  return 0;
}

static void gps_clear_fix(struct gps_fix_t *fixp) {
  fixp->mode = MODE_NOT_SEEN;
  fixp->pos.lat = fixp->pos.lon = NAN;
  fixp->alt = NAN;
  fixp->eph = NAN;
}

/* unpack a daemon response into a status structure */
static void gps_unpack(char *buf, struct gps_data_t *gpsdata) {
  char *ns, *sp, *tp;

  for(ns = strstr(buf,"GPSD"); ns; ns = strstr(ns+1, "GPSD")) {
    /* the following should execute each time we have a good next sp */
    for (sp = ns + 5; *sp != '\0'; sp = tp+1) {
      tp = sp + strcspn(sp, ",\r\n");
      if (*tp == '\0')
        tp--;
      else
        *tp = '\0';

      if (*sp == '0') {
	if (sp[2] == '?') {
	  gpsdata->set =
	    (gpsdata->set & SATELLITE_SET) | // fix for below
	    MODE_SET | STATUS_SET;  // this clears sat info??
	  gpsdata->status = STATUS_NO_FIX;
	  gps_clear_fix(&gpsdata->fix);
	} else {
	  struct gps_fix_t nf;
	  char tag[MAXTAGLEN+1], alt[20], eph[20], lat[20], lon[20], mode[2];
	  int st = sscanf(sp+2,
			  "%8s %*s %*s %19s %19s "
			  "%19s %19s %*s %*s %*s %*s "
			  "%*s %*s %*s %1s",
			  tag, lat, lon,
			  alt, eph,
			  mode);
	  if (st >= 5) {
#define DEFAULT(val) (val[0] == '?') ? NAN : g_ascii_strtod(val, NULL)
	    nf.pos.lat = DEFAULT(lat);
	    nf.pos.lon = DEFAULT(lon);
	    nf.eph = DEFAULT(eph);
	    nf.alt = DEFAULT(alt);
#undef DEFAULT
	    if (st >= 6)
	      nf.mode = (mode[0] == '?') ? MODE_NOT_SEEN : atoi(mode);
	    else
	      nf.mode = (alt[0] == '?') ? MODE_2D : MODE_3D;
	    gpsdata->fix = nf;
	    gpsdata->set |= LATLON_SET|MODE_SET;
	    gpsdata->status = STATUS_FIX;
	    gpsdata->set |= STATUS_SET;

	    if(alt[0] != '?')
	      gpsdata->set |= ALTITUDE_SET;
	  }
	}
      }
    }
  }
}

void gps_enable(appdata_t *appdata, gboolean enable) {
  if(appdata->settings)
    appdata->settings->enable_gps = enable;
}

gpointer gps_thread(gpointer data) {
  GnomeVFSFileSize bytes_read;
  char str[512];
  appdata_t *appdata = (appdata_t*)data;

  const char *msg = "o\r\n";   /* pos request */

  appdata->gps_state->gpsdata.set = 0;

  gboolean connected = FALSE;

  while(1) {
    if(appdata->settings && appdata->settings->enable_gps) {
      if(!connected) {
	printf("trying to connect\n");

	if(gps_connect(appdata->gps_state) < 0)
	  sleep(10);
	else
	  connected = TRUE;
      } else if(gnome_vfs_socket_write(appdata->gps_state->socket,
		msg, strlen(msg)+1, &bytes_read, NULL) == GNOME_VFS_OK) {

	/* update every second, wait here to make sure a complete */
	/* reply is received */
	sleep(1);

	if(bytes_read == (strlen(msg)+1)) {
	  if(gnome_vfs_socket_read(appdata->gps_state->socket,
	     str, sizeof(str)-1, &bytes_read, NULL) == GNOME_VFS_OK) {
	    str[bytes_read] = 0;

	    printf("msg: %s (%zu)\n", str, strlen(str));

	    g_mutex_lock(appdata->gps_state->mutex);

	    appdata->gps_state->gpsdata.set &=
	      ~(LATLON_SET|MODE_SET|STATUS_SET);

	    gps_unpack(str, &appdata->gps_state->gpsdata);
	    g_mutex_unlock(appdata->gps_state->mutex);
	  }
	}
      }
    } else {
      if(connected) {
	printf("stopping GPS connection due to user request\n");
	gnome_vfs_inet_connection_destroy(appdata->gps_state->iconn, NULL);

#ifdef ENABLE_GPSBT
	gpsbt_stop(&appdata->gps_state->context);
#endif
	connected = FALSE;
      } else
	sleep(1);
    }
  }

  printf("GPS thread ended???\n");
  return NULL;
}

void gps_init(appdata_t *appdata) {
  appdata->gps_state = g_new0(gps_state_t, 1);

  printf("GPS init: Using gpsd\n");

  /* start a new thread to listen to gpsd */
#if GLIB_CHECK_VERSION(2,32,0)
  appdata->gps_state->mutex = &appdata->gps_state->rmutex;
  g_mutex_init(appdata->gps_state->mutex);
  appdata->gps_state->thread_p =
    g_thread_try_new("gps", gps_thread, appdata, NULL);
#else
  appdata->gps_state->mutex = g_mutex_new();
  appdata->gps_state->thread_p =
    g_thread_create(gps_thread, appdata, FALSE, NULL);
#endif
}

void gps_release(appdata_t *appdata) {
#ifdef ENABLE_GPSBT
  gpsbt_stop(&appdata->gps_state->context);
#endif
#if GLIB_CHECK_VERSION(2,32,0)
  g_mutex_clear(appdata->gps_state->mutex);
  if (appdata->gps_state->thread_p)
    g_thread_unref(appdata->gps_state->thread_p);
#else
  g_mutex_free(appdata->gps_state->mutex);
#endif
  g_free(appdata->gps_state);
  appdata->gps_state = NULL;
}

gboolean gps_register_callback(appdata_t *appdata, GtkFunction cb) {
  if(appdata->track.handler_id) {
    if(cb == NULL) {
      gtk_timeout_remove(appdata->track.handler_id);
      appdata->track.handler_id = 0;
    }
    return TRUE;
  } else {
    if(cb != NULL) {
      appdata->track.handler_id = gtk_timeout_add(1000, cb, appdata);
    }
    return FALSE;
  }
}
