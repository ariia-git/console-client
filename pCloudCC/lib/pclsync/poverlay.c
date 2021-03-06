/* Copyright (c) 2013-2015 pCloud Ltd.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of pCloud Ltd nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL pCloud Ltd BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "pcompat.h"
#include "plibs.h"
#include "poverlay.h"
#include "pexternalstatus.h"
#include "pcache.h"

int overlays_running = 1;
int callbacks_running = 1;

#if defined(P_OS_WINDOWS)

#include "poverlay_win.c"

#elif defined(P_OS_LINUX)

//#include "poverlay_lin.c"
#include "poverlay_mac.c"

#elif defined(P_OS_MACOSX)

#include "poverlay_mac.c"

#else 

void overlay_main_loop(VOID){}
void instance_thread(LPVOID){}

#endif //defined(P_OS_WINDOWS)

poverlay_callback * callbacks;
static int callbacks_size = 15;
static const int calbacks_lower_band = 20;

#define CACHE_PREF "P_OVERLA_CACHE_PREFIX"
#define CACHE_PREF_LEN 21


typedef struct overlay_cache_{
  external_status stat;
  uint64_t timestamp;
} overlay_cache_t;

static int get_item_from_cache(const char* key, external_status* stat){
  int ketlen = strlen(key);
  char* reckey =  psync_malloc(ketlen + CACHE_PREF_LEN + 1);
  overlay_cache_t * rec = NULL;
  uint64_t now = 0;
  
  memcpy(reckey, CACHE_PREF, CACHE_PREF_LEN);
  memcpy(reckey + CACHE_PREF_LEN , key, ketlen + 1);
  
  if ((rec = (overlay_cache_t *) psync_cache_get(reckey))) {
    now = psync_millitime();
    if ((now - rec->timestamp) < 100) {
      *stat = rec->stat;
      return 1;
    } else {
      psync_free(rec);
    }
  }
  return 0;
}

static void add_item_to_cache(const char* key, external_status* stat){
  int ketlen = strlen(key);
  char* reckey =  psync_malloc(ketlen + CACHE_PREF_LEN + 1);
  overlay_cache_t * rec = psync_malloc(sizeof(overlay_cache_t));
  
  strncpy(reckey, CACHE_PREF, CACHE_PREF_LEN);
  strncpy(reckey + CACHE_PREF_LEN , key, ketlen);

  rec->stat = *stat;
  rec->timestamp = psync_millitime();
  psync_cache_add(key, rec, 1, psync_free, 1);
}

  

int psync_add_overlay_callback(int id, poverlay_callback callback) 
{
  poverlay_callback * callbacks_old = callbacks;
  int callbacks_size_old = callbacks_size;
  if (id < calbacks_lower_band)
    return -1;
  if (id > (calbacks_lower_band + callbacks_size)) {
     callbacks_size = id - calbacks_lower_band + 1;
     init_overlay_callbacks();
     memcpy(callbacks,callbacks_old, callbacks_size_old*sizeof(poverlay_callback));
     psync_free(callbacks_old);
  }
  callbacks[id - calbacks_lower_band] = callback;
  return 0;
}

void init_overlay_callbacks() {
  callbacks = (poverlay_callback *) psync_malloc(sizeof(poverlay_callback)*callbacks_size);
  memset(callbacks, 0, sizeof(poverlay_callback)*callbacks_size);
}

void psync_stop_overlays(){
  overlays_running = 0;
}
void psync_start_overlays(){
  overlays_running = 1;
}

void psync_stop_overlay_callbacks(){
  callbacks_running = 0;
}
void psync_start_overlay_callbacks(){
  callbacks_running = 1;
}

void get_answer_to_request(message *request, message *replay)
{
  char msg[4] = "Ok.";
  external_status stat = INVSYNC;
  msg[3] = '\0';
  //debug(D_NOTICE, "Client Request type [%u] len [%lu] string: [%s]", request->type, request->length, request->value);
  if (request->type < 20 ) {
    
    if (overlays_running && !get_item_from_cache(request->value, &stat)) {
     stat = do_psync_external_status(request->value);
    }
    
    if (stat == INSYNC) {
      replay->type = 10;
    }
    else if (stat == NOSYNC) {
      replay->type = 11;
    }
    else if (stat == INPROG) {

      replay->type = 12;
    }
    else {
      replay->type = 13;
      strncpy(msg,"No.\0",4);
    }
    replay->length = sizeof(message)+4;
    strncpy(replay->value, msg, 4);
    add_item_to_cache(request->value, &stat);
  } else if ((callbacks_running)&&(request->type < 36)) {
    int ind = request->type - 20;
    int ret = 0;
    if (callbacks[ind]) {
      if ((ret = callbacks[ind](request->value)) == 0) {
        replay->type = 0;
        replay->length = sizeof(message)+4;
        strncpy(replay->value, msg, 4);
      } else {
        replay->type = ret;
        strncpy(msg,"No.\0",4);
      }
      strncpy(replay->value, msg, 4);
      replay->length = sizeof(message)+4;
    } else {
      replay->type = 13;
      strncpy(replay->value, "No callback with this id registered.\0", 37);
      replay->length = sizeof(message)+37;
    }
  } else {
      replay->type = 13;
      strncpy(replay->value, "Invalid type.\0", 14);
      replay->length = sizeof(message)+14;
    }

}


int psync_overlays_running(){return overlays_running;}
int psync_ovr_callbacks_running(){return callbacks_running;}
