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
 * along with OSM2Go.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <curl/curl.h>
#include <string>

#include <osm2go_platform.h>

/**
 * @brief download from the given URL to file
 * @param parent widget for status messages
 * @param url the request URL
 * @param filename output filename
 * @param title window title string for the download window
 * @param compress if gzip compression of the data should be enabled
 *
 * @returns if the request was successful
 */
bool net_io_download_file(osm2go_platform::Widget *parent,
                          const std::string &url, const std::string &filename,
                          const char *title, bool compress = false);
/**
 * @brief download from the given URL to memory
 * @param parent widget for status messages
 * @param url the request URL
 * @param data where output will be stored
 * @returns if the request was successful
 *
 * The data is possibly gzip encoded.
 */
bool net_io_download_mem(osm2go_platform::Widget *parent, const std::string &url,
                         std::string &data, const char *title);

/**
 * @brief translate HTTP status code to string
 * @param id the HTTP status code
 */
const char *http_message(int id);

/**
 * @brief check if the given memory contains a valid gzip header
 */
bool check_gzip(const char *mem, const size_t len);

struct curl_deleter {
  inline void operator()(CURL *curl)
  { curl_easy_cleanup(curl); }
};

struct curl_slist_deleter {
  inline void operator()(curl_slist *slist)
  { curl_slist_free_all(slist); }
};
