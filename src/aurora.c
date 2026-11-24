#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define APP_NAME "AURORA"
#define APP_VERSION "0.4.0"
#define W 720
#define H 720
#define MAX_TRACKS 12000
#define PATH_HASH_SIZE 32768
#define MAX_PATH 1024
#define MAX_TEXT 512
#define VIOLET_R 170
#define VIOLET_G 100
#define VIOLET_B 245

typedef struct {
    char path[MAX_PATH];
    char title[MAX_TEXT];
    char artist[MAX_TEXT];
    char album_artist[MAX_TEXT];
    char album[MAX_TEXT];
    char genre[MAX_TEXT];
    char year[16];
    char codec[64];
    char sample_rate[32];
    char bit_rate[32];
    char bit_depth[16];
    char track_no[16];
    double duration;
    time_t mtime;
    int seen;
} Track;

typedef enum { PAGE_HOME, PAGE_LIST, PAGE_NOW, PAGE_SETTINGS, PAGE_FOLDERS, PAGE_PLAYLISTS } Page;
typedef enum { LIST_ALL, LIST_ARTISTS, LIST_ALBUMS, LIST_PLAYLISTS, LIST_RECENT } ListKind;

static Track tracks[MAX_TRACKS];
static int track_count = 0;
static int path_hash_slots[PATH_HASH_SIZE];
static int cache_loaded = 0;
static int view_indices[MAX_TRACKS];
static int view_count = 0;
static int selected = 0, scroll_pos = 0;
static Page page = PAGE_HOME;
static ListKind list_kind = LIST_ALL;
static char page_title[64] = "HOME";
static char music_folder[MAX_PATH] = "/roms/music";
static char config_dir[MAX_PATH];
static char playlist_dir[MAX_PATH];
static char cache_path[MAX_PATH];
static char sort_order[32] = "Name";
static int crossfade_enabled = 0;
static int crossfade_mode = 2;
static int crossfade_seconds = 4;
static int current_track = -1;
static int queue_pos = -1;
static int queue_indices[MAX_TRACKS];
static int queue_count = 0;
static int volume = 85;
static int actual_volume = 85;
static int transition_target = -1, fade_stage = 0, fade_start_value = 0;
static Uint32 fade_started = 0;
static int paused = 0;
static double position_sec = 0.0;
static double duration_sec = 0.0;
static char toast[192] = "";
static Uint32 toast_time = 0;
static char folder_current[MAX_PATH] = "/";
static char folder_entries[512][MAX_PATH];
static int folder_count = 0;
static int folder_selected = 0;
static char active_playlist[MAX_PATH] = "";
static char player_socket[128];
static pid_t mpv_pid = -1;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Joystick *joystick = NULL;
static SDL_Surface *cover_surface = NULL;
static SDL_Texture *cover_texture = NULL;
static SDL_Color accent = {125, 82, 160, 255};
static int running = 1;
static char playlist_name[80] = "";
static int text_entry = 0;
static int shuffle_enabled = 0;
static int repeat_mode = 0; /* 0=off, 1=all, 2=one */
static int led_mode = 0;    /* 0=off, 1=album colour, 2=ambient pulse */
static int led_available = 0;
static int led_driver = 0; /* 1=ArkOS4Clone joyled sysfs, 2=Batocera LED helper */
static Page page_before_now = PAGE_HOME;
static Uint32 volume_overlay_until = 0;
static int volume_overlay_value = 85;
static Uint32 seek_overlay_until = 0;
static int seek_overlay_direction = 0;
static int held_dirs[4] = {0,0,0,0};
static Uint32 held_since[4] = {0,0,0,0};
static Uint32 held_repeat[4] = {0,0,0,0};
static int held_long[4] = {0,0,0,0};
static int hat_dirs = 0;
static int select_pressed = 0, start_pressed = 0;
static int stick_dirs[4] = {0,0,0,0};
static Uint32 stick_repeat[4] = {0,0,0,0};
static Uint32 last_status_poll = 0, last_led_poll = 0;
static int system_battery = -1, system_wifi = 0, system_charging = 0;
static int last_led_r = -1, last_led_g = -1, last_led_b = -1;
static SDL_GameController *game_controller = NULL;
typedef struct { int track; SDL_Texture *texture; } Thumb;
static Thumb thumbs[12];
static int thumb_cursor = 0;
static pthread_t thumb_worker;
static pthread_mutex_t thumb_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t thumb_cond = PTHREAD_COND_INITIALIZER;
static int thumb_worker_running = 0, thumb_worker_stop = 0;
static int thumb_queue[MAX_TRACKS], thumb_qhead = 0, thumb_qtail = 0;
static unsigned char thumb_queued[MAX_TRACKS], thumb_done[MAX_TRACKS];

/* Forward declarations for the controller/navigation helpers. */
static void handle_key(SDL_Keycode key);
static void start_new_playlist(void);
static SDL_Texture *get_thumb(int idx);

/* A compact built-in 5x7 uppercase bitmap font. No SDL_ttf or bundled font needed. */
static const unsigned char glyphs[48][5] = {
 {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36}, {0x3e,0x41,0x41,0x41,0x22},
 {0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
 {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f}, {0x00,0x41,0x7f,0x41,0x00},
 {0x20,0x40,0x41,0x3f,0x01}, {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
 {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f}, {0x3e,0x41,0x41,0x41,0x3e},
 {0x7f,0x09,0x09,0x09,0x06}, {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01}, {0x3f,0x40,0x40,0x40,0x3f},
 {0x1f,0x20,0x40,0x20,0x1f}, {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
 {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
 {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4b,0x31}, {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
 {0x06,0x49,0x49,0x29,0x1e}, {0x00,0x00,0x00,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
 {0x00,0x60,0x60,0x00,0x00}, {0x00,0x36,0x36,0x00,0x00},
 {0x08,0x08,0x08,0x08,0x08}, {0x60,0x10,0x08,0x04,0x03},
 {0x40,0x40,0x40,0x40,0x7f}, {0x08,0x08,0x3e,0x08,0x08},
 {0x02,0x01,0x51,0x09,0x06}, {0x00,0x00,0x07,0x00,0x00},
 {0x00,0x60,0x30,0x00,0x00}, {0x1c,0x22,0x41,0x00,0x00}
};

static void draw_text_raw(const char *s, int x, int y, int scale, SDL_Color color, int centered, int max_width) {
    if (!s) return;
    int len = (int)strlen(s);
    int total = len * 6 * scale;
    if (max_width > 0 && total > max_width) {
        int allowed = max_width / (6 * scale);
        if (allowed < 1) allowed = 1;
        len = allowed;
        total = len * 6 * scale;
    }
    if (centered) x -= total / 2;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        int idx = -1;
        if (c >= 'A' && c <= 'Z') idx = c - 'A';
        else if (c >= '0' && c <= '9') idx = 26 + c - '0';
        else if (c == ' ') idx = 36;
        else if (c == '-') idx = 37;
        else if (c == '.') idx = 38;
        else if (c == ':') idx = 39;
        else if (c == '|') idx = 40;
        else if (c == '/') idx = 41;
        else if (c == '_') idx = 42;
        else if (c == '+') idx = 43;
        else if (c == '?') idx = 44;
        else if (c == '\'') idx = 45;
        else if (c == ',') idx = 46;
        else if (c == '(' || c == ')') idx = 47;
        for (int col = 0; col < 5; col++) {
            unsigned char bits = idx >= 0 ? glyphs[idx][col] : 0;
            for (int row = 0; row < 7; row++) {
                if (bits & (1u << row)) {
                    SDL_Rect r = {x + (i * 6 + col) * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &r);
                }
            }
        }
    }
}
static void text(const char *s, int x, int y, int size, SDL_Color c, int centered) {
    int scale = size <= 12 ? 1 : size <= 20 ? 2 : 3;
    draw_text_raw(s, x, y, scale, c, centered, 680);
}
static void text_width(const char *s, int x, int y, int size, SDL_Color c, int max_width) {
    int scale = size <= 12 ? 1 : size <= 20 ? 2 : 3;
    draw_text_raw(s, x, y, scale, c, 0, max_width);
}
static SDL_Color rgb(int r,int g,int b) { SDL_Color c={(Uint8)r,(Uint8)g,(Uint8)b,255}; return c; }
static void fill_rect(int x,int y,int w,int h,SDL_Color c) { SDL_Rect r={x,y,w,h}; SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); SDL_RenderFillRect(renderer,&r); }
static void round_panel(int x,int y,int w,int h,SDL_Color c) {
    fill_rect(x+10,y,w-20,h,c); fill_rect(x,y+10,w,h-20,c);
    SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);
    for(int i=0;i<10;i++){ SDL_RenderDrawPoint(renderer,x+10-i,y+10-i); SDL_RenderDrawPoint(renderer,x+w-11+i,y+10-i); SDL_RenderDrawPoint(renderer,x+10-i,y+h-11+i); SDL_RenderDrawPoint(renderer,x+w-11+i,y+h-11+i); }
}
static void line(int x1,int y1,int x2,int y2,SDL_Color c) { SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); SDL_RenderDrawLine(renderer,x1,y1,x2,y2); }
static void circle(int cx,int cy,int radius,SDL_Color c,int filled) {
    SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);
    for(int dy=-radius;dy<=radius;dy++){
        int dx=(int)sqrt((double)radius*radius-(double)dy*dy);
        if(filled) SDL_RenderDrawLine(renderer,cx-dx,cy+dy,cx+dx,cy+dy);
        else { SDL_RenderDrawPoint(renderer,cx-dx,cy+dy); SDL_RenderDrawPoint(renderer,cx+dx,cy+dy); }
    }
}
static void toast_msg(const char *s) { snprintf(toast,sizeof(toast),"%s",s); toast_time=SDL_GetTicks()+2300; }
static void shell_quote(const char *s,char *out,size_t cap) {
    size_t n=0; if(cap<3){if(cap)out[0]=0;return;} out[n++]='\'';
    for(;*s && n+5<cap;s++){ if(*s=='\''){out[n++]='\'';out[n++]='\\';out[n++]='\'';out[n++]='\'';} else out[n++]=*s; }
    out[n++]='\''; out[n]=0;
}
static int is_audio(const char *name) {
    const char *dot=strrchr(name,'.'); if(!dot)return 0; dot++;
    const char *exts[]={"mp3","flac","wav","wave","m4a","aac","alac","ogg","oga","opus","wma","aiff","aif","ape","wv","dsf","dff","mpc","tta","mka","mp4","3gp",NULL};
    for(int i=0;exts[i];i++) {
        if(!strcasecmp(dot,exts[i])) return 1;
    }
    return 0;
}
static void strip_quotes(char *s) {
    size_t n=strlen(s); if(n>=2 && ((s[0]=='"'&&s[n-1]=='"')||(s[0]=='\''&&s[n-1]=='\''))){memmove(s,s+1,n-2);s[n-2]=0;}
}
static void run_ffprobe(Track *t) {
    char q[MAX_PATH*2],cmd[4096]; shell_quote(t->path,q,sizeof(q));
    snprintf(cmd,sizeof(cmd),"ffprobe -v error -show_entries format=duration:format_tags=title,artist,album_artist,album,date,genre,track:stream=codec_name,sample_rate,bit_rate,bits_per_raw_sample,bits_per_sample -of default=noprint_wrappers=1 %s 2>/dev/null",q);
    FILE *p=popen(cmd,"r"); if(!p)return; char linebuf[1024];
    while(fgets(linebuf,sizeof(linebuf),p)){
        char *eq=strchr(linebuf,'='); if(!eq)continue; *eq++=0; eq[strcspn(eq,"\r\n")]=0; strip_quotes(eq);
        if(!strcmp(linebuf,"TAG:title"))snprintf(t->title,sizeof(t->title),"%s",eq);
        else if(!strcmp(linebuf,"TAG:artist"))snprintf(t->artist,sizeof(t->artist),"%s",eq);
        else if(!strcmp(linebuf,"TAG:album_artist"))snprintf(t->album_artist,sizeof(t->album_artist),"%s",eq);
        else if(!strcmp(linebuf,"TAG:album"))snprintf(t->album,sizeof(t->album),"%s",eq);
        else if(!strcmp(linebuf,"TAG:genre"))snprintf(t->genre,sizeof(t->genre),"%s",eq);
        else if(!strcmp(linebuf,"TAG:date")){snprintf(t->year,sizeof(t->year),"%.4s",eq);}
        else if(!strcmp(linebuf,"TAG:track"))snprintf(t->track_no,sizeof(t->track_no),"%s",eq);
        else if(!strcmp(linebuf,"duration"))t->duration=strtod(eq,NULL);
        else if(!strcmp(linebuf,"codec_name"))snprintf(t->codec,sizeof(t->codec),"%s",eq);
        else if(!strcmp(linebuf,"sample_rate"))snprintf(t->sample_rate,sizeof(t->sample_rate),"%s",eq);
        else if(!strcmp(linebuf,"bit_rate"))snprintf(t->bit_rate,sizeof(t->bit_rate),"%s",eq);
        else if(!strcmp(linebuf,"bits_per_raw_sample")||!strcmp(linebuf,"bits_per_sample")){if(!t->bit_depth[0]&&atoi(eq)>0)snprintf(t->bit_depth,sizeof(t->bit_depth),"%s",eq);}
    }
    pclose(p);
    if(!t->title[0]){const char *b=strrchr(t->path,'/');b=b?b+1:t->path;snprintf(t->title,sizeof(t->title),"%s",b);char *dot=strrchr(t->title,'.');if(dot)*dot=0;}
    if(!t->artist[0])snprintf(t->artist,sizeof(t->artist),"UNKNOWN ARTIST");
    if(!t->album_artist[0])snprintf(t->album_artist,sizeof(t->album_artist),"%s",t->artist);
    if(!t->album[0])snprintf(t->album,sizeof(t->album),"UNKNOWN ALBUM");
    if(!t->genre[0])snprintf(t->genre,sizeof(t->genre),"UNKNOWN");
    if(!t->year[0])snprintf(t->year,sizeof(t->year),"----");
    if(!t->codec[0])snprintf(t->codec,sizeof(t->codec),"%s",strrchr(t->path,'.')?strrchr(t->path,'.')+1:"AUDIO");
}
static unsigned long path_hash_value(const char *path) {
    unsigned long h=5381;for(const unsigned char *p=(const unsigned char*)path;*p;p++)h=((h<<5)+h)+*p;return h;
}
static int lookup_track_path(const char *path) {
    unsigned long slot=path_hash_value(path)%PATH_HASH_SIZE;
    for(int n=0;n<PATH_HASH_SIZE;n++){int idx=path_hash_slots[slot];if(idx<0)return -1;if(!strcmp(tracks[idx].path,path))return idx;slot=(slot+1)%PATH_HASH_SIZE;}
    return -1;
}
static void insert_track_path(int idx) {
    unsigned long slot=path_hash_value(tracks[idx].path)%PATH_HASH_SIZE;
    for(int n=0;n<PATH_HASH_SIZE;n++){if(path_hash_slots[slot]<0){path_hash_slots[slot]=idx;return;}slot=(slot+1)%PATH_HASH_SIZE;}
}
static void rebuild_path_hash(void) {
    for(int i=0;i<PATH_HASH_SIZE;i++)path_hash_slots[i]=-1;
    for(int i=0;i<track_count;i++)insert_track_path(i);
}
static void clean_cache_field(char *s) {
    for(;*s;s++)if(*s=='\t'||*s=='\r'||*s=='\n')*s=' ';
}
static void load_library_cache(void) {
    for(int i=0;i<PATH_HASH_SIZE;i++)path_hash_slots[i]=-1;
    FILE *f=fopen(cache_path,"r");if(!f)return;char ln[8192];
    while(track_count<MAX_TRACKS&&fgets(ln,sizeof(ln),f)){
        ln[strcspn(ln,"\r\n")]=0;
        char *fields[14]={0};int count=1;fields[0]=ln;
        for(int k=1;k<14;k++){char *sep=strchr(fields[k-1],'\t');if(!sep)break;*sep=0;fields[k]=sep+1;count++;}
        if(count<14||!fields[0][0])continue;
        Track *t=&tracks[track_count];memset(t,0,sizeof(*t));
        snprintf(t->path,sizeof(t->path),"%s",fields[0]);t->mtime=(time_t)strtoll(fields[1],NULL,10);t->duration=strtod(fields[2],NULL);
        snprintf(t->title,sizeof(t->title),"%s",fields[3]);snprintf(t->artist,sizeof(t->artist),"%s",fields[4]);
        snprintf(t->album_artist,sizeof(t->album_artist),"%s",fields[5]);snprintf(t->album,sizeof(t->album),"%s",fields[6]);
        snprintf(t->genre,sizeof(t->genre),"%s",fields[7]);snprintf(t->year,sizeof(t->year),"%s",fields[8]);
        snprintf(t->codec,sizeof(t->codec),"%s",fields[9]);snprintf(t->sample_rate,sizeof(t->sample_rate),"%s",fields[10]);
        snprintf(t->bit_rate,sizeof(t->bit_rate),"%s",fields[11]);snprintf(t->bit_depth,sizeof(t->bit_depth),"%s",fields[12]);
        snprintf(t->track_no,sizeof(t->track_no),"%s",fields[13]);insert_track_path(track_count);track_count++;
    }
    fclose(f);
}
static void save_library_cache(void) {
    char tmp[MAX_PATH];snprintf(tmp,sizeof(tmp),"%s.tmp",cache_path);FILE *f=fopen(tmp,"w");if(!f)return;
    for(int i=0;i<track_count;i++){
        Track *t=&tracks[i];char vals[12][MAX_TEXT];
        snprintf(vals[0],sizeof(vals[0]),"%s",t->title);snprintf(vals[1],sizeof(vals[1]),"%s",t->artist);
        snprintf(vals[2],sizeof(vals[2]),"%s",t->album_artist);snprintf(vals[3],sizeof(vals[3]),"%s",t->album);
        snprintf(vals[4],sizeof(vals[4]),"%s",t->genre);snprintf(vals[5],sizeof(vals[5]),"%s",t->year);
        snprintf(vals[6],sizeof(vals[6]),"%s",t->codec);snprintf(vals[7],sizeof(vals[7]),"%s",t->sample_rate);
        snprintf(vals[8],sizeof(vals[8]),"%s",t->bit_rate);snprintf(vals[9],sizeof(vals[9]),"%s",t->bit_depth);snprintf(vals[10],sizeof(vals[10]),"%s",t->track_no);
        for(int k=0;k<11;k++)clean_cache_field(vals[k]);
        char path[MAX_PATH];snprintf(path,sizeof(path),"%s",t->path);clean_cache_field(path);
        fprintf(f,"%s\t%lld\t%.3f",path,(long long)t->mtime,t->duration);
        for(int k=0;k<11;k++)fprintf(f,"\t%s",vals[k]);
        fputc('\n',f);
    }
    fclose(f);rename(tmp,cache_path);
}

static void scan_dir(const char *folder,int depth) {
    if(depth>24 || track_count>=MAX_TRACKS)return;
    DIR *d=opendir(folder); if(!d)return; struct dirent *e;
    while((e=readdir(d)) && track_count<MAX_TRACKS){ if(e->d_name[0]=='.')continue;
        char p[MAX_PATH]; if(snprintf(p,sizeof(p),"%s/%s",folder,e->d_name)>=(int)sizeof(p))continue;
        struct stat st; if(lstat(p,&st)!=0)continue;
        if(S_ISDIR(st.st_mode)){scan_dir(p,depth+1);continue;}
        if(!S_ISREG(st.st_mode)||!is_audio(e->d_name))continue;
        int idx=lookup_track_path(p);
        if(idx>=0){Track *t=&tracks[idx];t->seen=1;if(t->mtime!=(time_t)st.st_mtime){memset(t,0,sizeof(*t));snprintf(t->path,sizeof(t->path),"%s",p);t->mtime=st.st_mtime;t->seen=1;run_ffprobe(t);}continue;}
        if(track_count>=MAX_TRACKS)break;
        idx=track_count++;Track *t=&tracks[idx];memset(t,0,sizeof(*t));snprintf(t->path,sizeof(t->path),"%s",p);t->mtime=st.st_mtime;t->seen=1;insert_track_path(idx);run_ffprobe(t);
    }
    closedir(d);
}
static int cmp_field(const Track *a,const Track *b) {
    if(!strcasecmp(sort_order,"Genre"))return strcasecmp(a->genre,b->genre);
    if(!strcasecmp(sort_order,"Album Artist"))return strcasecmp(a->album_artist,b->album_artist);
    if(!strcasecmp(sort_order,"Artist"))return strcasecmp(a->artist,b->artist);
    if(!strcasecmp(sort_order,"Album"))return strcasecmp(a->album,b->album);
    if(!strcasecmp(sort_order,"Year"))return strcasecmp(a->year,b->year);
    if(!strcasecmp(sort_order,"Duration"))return (a->duration>b->duration)-(a->duration<b->duration);
    return strcasecmp(a->title,b->title);
}
static void rebuild_view(void) {
    view_count=0;
    if(list_kind==LIST_ALL||list_kind==LIST_RECENT){for(int i=0;i<track_count;i++)view_indices[view_count++]=i;}
    else if(list_kind==LIST_ARTISTS){
        for(int i=0;i<track_count;i++){int seen=0;for(int j=0;j<view_count;j++)if(!strcasecmp(tracks[i].album_artist,tracks[view_indices[j]].album_artist)){seen=1;break;}if(!seen)view_indices[view_count++]=i;}
    } else if(list_kind==LIST_ALBUMS){
        for(int i=0;i<track_count;i++){int seen=0;for(int j=0;j<view_count;j++)if(!strcasecmp(tracks[i].album,tracks[view_indices[j]].album)&&!strcasecmp(tracks[i].album_artist,tracks[view_indices[j]].album_artist)){seen=1;break;}if(!seen)view_indices[view_count++]=i;}
    }
    for(int i=0;i<view_count;i++)for(int j=i+1;j<view_count;j++){
        int cmp=0;
        if(list_kind==LIST_ARTISTS)cmp=strcasecmp(tracks[view_indices[i]].album_artist,tracks[view_indices[j]].album_artist);
        else if(list_kind==LIST_ALBUMS)cmp=strcasecmp(tracks[view_indices[i]].album,tracks[view_indices[j]].album);
        else if(list_kind==LIST_RECENT)cmp=(tracks[view_indices[i]].mtime<tracks[view_indices[j]].mtime)?1:(tracks[view_indices[i]].mtime>tracks[view_indices[j]].mtime?-1:0);
        else cmp=cmp_field(&tracks[view_indices[i]],&tracks[view_indices[j]]);
        if(cmp>0){int x=view_indices[i];view_indices[i]=view_indices[j];view_indices[j]=x;}
    }
    selected=0;scroll_pos=0;
}
static void save_config(void) {
    char p[MAX_PATH];snprintf(p,sizeof(p),"%s/settings.conf",config_dir);FILE *f=fopen(p,"w");if(!f)return;
    fprintf(f,"music_folder=%s\nvolume=%d\nsort_order=%s\ncrossfade_enabled=%d\ncrossfade_mode=%d\ncrossfade_seconds=%d\nshuffle_enabled=%d\nrepeat_mode=%d\nled_mode=%d\n",music_folder,volume,sort_order,crossfade_enabled,crossfade_mode,crossfade_seconds,shuffle_enabled,repeat_mode,led_mode);
    fclose(f);
}
static void load_config(void) {
    const char *home=getenv("HOME");if(!home)home="/home/ark";
    snprintf(config_dir,sizeof(config_dir),"%s/.config/aurora-r36",home);snprintf(playlist_dir,sizeof(playlist_dir),"%s/playlists",config_dir);snprintf(cache_path,sizeof(cache_path),"%s/library-cache.tsv",config_dir);
    mkdir("/tmp/aurora-r36",0755);mkdir(config_dir,0755);mkdir(playlist_dir,0755);
    char p[MAX_PATH];snprintf(p,sizeof(p),"%s/settings.conf",config_dir);FILE *f=fopen(p,"r");if(!f)return;
    char ln[2048];while(fgets(ln,sizeof(ln),f)){char *eq=strchr(ln,'=');if(!eq)continue;*eq++=0;eq[strcspn(eq,"\r\n")]=0;
        if(!strcmp(ln,"music_folder"))snprintf(music_folder,sizeof(music_folder),"%s",eq);
        else if(!strcmp(ln,"volume"))volume=atoi(eq);
        else if(!strcmp(ln,"sort_order"))snprintf(sort_order,sizeof(sort_order),"%s",eq);
        else if(!strcmp(ln,"crossfade_enabled"))crossfade_enabled=atoi(eq);
        else if(!strcmp(ln,"crossfade_mode"))crossfade_mode=atoi(eq);
        else if(!strcmp(ln,"crossfade_seconds"))crossfade_seconds=atoi(eq);
        else if(!strcmp(ln,"shuffle_enabled"))shuffle_enabled=atoi(eq);
        else if(!strcmp(ln,"repeat_mode"))repeat_mode=atoi(eq);
        else if(!strcmp(ln,"led_mode"))led_mode=atoi(eq);
    }fclose(f);
    if(volume<0)volume=0;
    if(volume>100)volume=100;
    if(crossfade_seconds<1)crossfade_seconds=1;
    if(crossfade_seconds>12)crossfade_seconds=12;
    if(repeat_mode<0||repeat_mode>2)repeat_mode=0;
    if(led_mode<0||led_mode>2)led_mode=0;
}
static int mpv_send(const char *json,char *response,size_t cap) {
    struct sockaddr_un addr; int s=socket(AF_UNIX,SOCK_STREAM,0); if(s<0)return -1;
    memset(&addr,0,sizeof(addr));addr.sun_family=AF_UNIX;snprintf(addr.sun_path,sizeof(addr.sun_path),"%s",player_socket);
    struct timeval tv={0,200000};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    if(connect(s,(struct sockaddr*)&addr,sizeof(addr))<0){close(s);return -1;}
    char buf[4096];snprintf(buf,sizeof(buf),"%s\n",json);send(s,buf,strlen(buf),0);
    if(response&&cap){int n=(int)recv(s,response,cap-1,0);if(n>0)response[n]=0;else response[0]=0;}
    close(s);return 0;
}
static int mpv_start(void) {
    snprintf(player_socket,sizeof(player_socket),"/tmp/aurora-mpv-%d.sock",getpid());unlink(player_socket);
    char q[256];snprintf(q,sizeof(q),"--input-ipc-server=%s",player_socket);
    mpv_pid=fork();if(mpv_pid==0){execlp("mpv","mpv","--idle=yes","--no-terminal","--no-video","--audio-display=no","--force-window=no","--osd-level=0","--keep-open=no","--volume=85",q,(char*)NULL);_exit(127);}
    if(mpv_pid<0)return 0;
    for(int i=0;i<60;i++){if(access(player_socket,F_OK)==0)return 1;SDL_Delay(50);}return 0;
}
static void mpv_cmd(const char *cmd) { mpv_send(cmd,NULL,0); }
static double mpv_get_number(const char *prop,double fallback) {
    char req[256],resp[1024];snprintf(req,sizeof(req),"{\"command\":[\"get_property\",\"%s\"]}",prop);
    if(mpv_send(req,resp,sizeof(resp))<0)return fallback;
    char *p=strstr(resp,"\"data\"");if(!p)return fallback;p=strchr(p,':');if(!p)return fallback;p++;
    while(*p==' '||*p=='\t')p++;
    if(!strncmp(p,"null",4))return fallback;
    if(!strncmp(p,"true",4))return 1.0;
    if(!strncmp(p,"false",5))return 0.0;
    return strtod(p,NULL);
}
static void json_escape(const char *in,char *out,size_t cap) {size_t n=0;for(;*in&&n+3<cap;in++){if(*in=='\\'||*in=='"')out[n++]='\\';if((unsigned char)*in>=32)out[n++]=*in;}out[n]=0;}
static void mpv_play_path(const char *path) {
    char e[MAX_PATH*2],cmd[MAX_PATH*2+128];json_escape(path,e,sizeof(e));snprintf(cmd,sizeof(cmd),"{\"command\":[\"loadfile\",\"%s\",\"replace\"]}",e);mpv_cmd(cmd);mpv_cmd("{\"command\":[\"set_property\",\"pause\",false]}");paused=0;
}
static void cache_cover(int idx) {
    if(cover_texture){SDL_DestroyTexture(cover_texture);cover_texture=NULL;}if(cover_surface){SDL_FreeSurface(cover_surface);cover_surface=NULL;}
    if(idx<0||idx>=track_count)return;
    char q[MAX_PATH*2],out[MAX_PATH],cmd[4096];unsigned long h=5381;const char *p=tracks[idx].path;for(;*p;p++)h=((h<<5)+h)+(unsigned char)*p;
    snprintf(out,sizeof(out),"/tmp/aurora-r36/cover-%lx.bmp",h);
    if(access(out,R_OK)!=0){shell_quote(tracks[idx].path,q,sizeof(q));snprintf(cmd,sizeof(cmd),"ffmpeg -v error -y -i %s -map 0:v:0 -frames:v 1 -vf 'scale=420:420:force_original_aspect_ratio=decrease,pad=420:420:(ow-iw)/2:(oh-ih)/2:color=black' %s 2>/dev/null",q,out);
        /* Fall back to folder artwork when there is no embedded picture. */
        int rc=system(cmd);if(rc!=0){char folder[MAX_PATH];snprintf(folder,sizeof(folder),"%s",tracks[idx].path);char *slash=strrchr(folder,'/');if(slash)*slash=0;
            const char *names[]={"cover.jpg","folder.jpg","Cover.jpg","Folder.jpg","cover.png","folder.png",NULL};
            for(int i=0;names[i];i++){char art[MAX_PATH];snprintf(art,sizeof(art),"%s/%s",folder,names[i]);if(access(art,R_OK)==0){shell_quote(art,q,sizeof(q));snprintf(cmd,sizeof(cmd),"ffmpeg -v error -y -i %s -vf 'scale=420:420:force_original_aspect_ratio=decrease,pad=420:420:(ow-iw)/2:(oh-ih)/2:color=black' %s 2>/dev/null",q,out);system(cmd);break;}}
        }
    }
    cover_surface=SDL_LoadBMP(out);if(cover_surface){cover_texture=SDL_CreateTextureFromSurface(renderer,cover_surface);
        unsigned int hist[32768]={0};unsigned int best=0;int best_bin=0;
        for(int y=0;y<cover_surface->h;y+=8)for(int x=0;x<cover_surface->w;x+=8){
            Uint32 px=0;memcpy(&px,(Uint8*)cover_surface->pixels+y*cover_surface->pitch+x*cover_surface->format->BytesPerPixel,cover_surface->format->BytesPerPixel);
            Uint8 rr,gg,bb;SDL_GetRGB(px,cover_surface->format,&rr,&gg,&bb);
            int bin=((rr>>3)<<10)|((gg>>3)<<5)|(bb>>3);unsigned int n=++hist[bin];if(n>best){best=n;best_bin=bin;}
        }
        accent.r=(Uint8)((((best_bin>>10)&31)<<3)+4);accent.g=(Uint8)((((best_bin>>5)&31)<<3)+4);accent.b=(Uint8)(((best_bin&31)<<3)+4);
    } else accent=rgb(125,82,160);
}
static void mpv_set_volume(int v) {
    if(v<0)v=0;
    if(v>100)v=100;
    actual_volume=v;char c[128];
    snprintf(c,sizeof(c),"{\"command\":[\"set_property\",\"volume\",%d]}",v);mpv_cmd(c);
}
static double fade_curve(double t) {
    if(t<0)t=0;
    if(t>1)t=1;
    switch(crossfade_mode){
        case 0: {double s=sin(t*1.57079632679);return s*s;} /* beat-style soft ramp */
        case 1: return sqrt(t); /* vocal-style gentle early fade */
        case 3: return t*t*(3.0-2.0*t); /* auto-mix smoothstep */
        default: return t;
    }
}
static void start_track_now(int idx,int fade_in) {
    if(idx<0||idx>=track_count)return;
    if(page!=PAGE_NOW)page_before_now=page;
    transition_target=-1;fade_stage=0;current_track=idx;position_sec=0;duration_sec=tracks[idx].duration;cache_cover(idx);mpv_play_path(tracks[idx].path);page=PAGE_NOW;
    if(fade_in&&crossfade_enabled){mpv_set_volume(0);fade_stage=2;fade_started=SDL_GetTicks();fade_start_value=0;}
    else mpv_set_volume(volume);
}
static void play_index(int idx) {
    if(idx<0||idx>=track_count)return;
    if(crossfade_enabled&&current_track>=0&&current_track!=idx){
        if(page!=PAGE_NOW)page_before_now=page;
        transition_target=idx;fade_stage=1;fade_started=SDL_GetTicks();fade_start_value=actual_volume;page=PAGE_NOW;return;
    }
    start_track_now(idx,crossfade_enabled&&current_track<0);
}
static int random_track_from_queue(void) {
    if(queue_count<=0)return track_count>0?rand()%track_count:-1;
    if(queue_count==1)return queue_indices[0];
    int p=queue_pos;for(int tries=0;tries<8&&p==queue_pos;tries++)p=rand()%queue_count;
    queue_pos=p;return queue_indices[p];
}
static void play_next(int dir) {
    int next=-1;
    if(shuffle_enabled){next=random_track_from_queue();}
    else if(queue_count>0){int p=queue_pos+dir;if(p<0)p=queue_count-1;if(p>=queue_count)p=0;queue_pos=p;next=queue_indices[p];}
    else if(track_count){int i=current_track<0?0:current_track+dir;if(i<0)i=track_count-1;if(i>=track_count)i=0;next=i;}
    if(next>=0)play_index(next);
}
static void play_next_automatic(void) {
    int next=-1;
    if(current_track<0)return;
    if(repeat_mode==2){start_track_now(current_track,crossfade_enabled);return;}
    if(shuffle_enabled){next=random_track_from_queue();}
    else if(queue_count>0){int p=queue_pos+1;if(p>=queue_count){if(repeat_mode==0){mpv_cmd("{\"command\":[\"stop\"]}");paused=1;return;}p=0;}queue_pos=p;next=queue_indices[p];}
    else if(track_count){int i=current_track+1;if(i>=track_count){if(repeat_mode==0){mpv_cmd("{\"command\":[\"stop\"]}");paused=1;return;}i=0;}next=i;}
    if(next>=0)start_track_now(next,crossfade_enabled);
}
static void update_fade(void) {
    if(!fade_stage)return;
    Uint32 now=SDL_GetTicks();int duration=crossfade_seconds*500;if(duration<500)duration=500;
    double t=(double)(now-fade_started)/(double)duration;if(t>1)t=1;
    double curve=fade_curve(t);
    if(fade_stage==1){int v=(int)lrint((double)fade_start_value*(1.0-curve));mpv_set_volume(v);
        if(t>=1.0){int target=transition_target;transition_target=-1;start_track_now(target,1);}
    }else if(fade_stage==2){int v=(int)lrint((double)volume*curve);mpv_set_volume(v);if(t>=1.0){mpv_set_volume(volume);fade_stage=0;}}
}
static void reload_library(void) {
    if(!cache_loaded){load_library_cache();cache_loaded=1;}
    for(int i=0;i<track_count;i++)tracks[i].seen=0;
    memset(thumb_done,0,sizeof(thumb_done));memset(thumb_queued,0,sizeof(thumb_queued));
    char p[MAX_PATH];snprintf(p,sizeof(p),"%s",music_folder);scan_dir(p,0);
    int kept=0;for(int i=0;i<track_count;i++)if(tracks[i].seen){if(kept!=i)tracks[kept]=tracks[i];kept++;}
    track_count=kept;rebuild_path_hash();save_library_cache();rebuild_view();
    char msg[160];snprintf(msg,sizeof(msg),"SCANNED %d TRACKS",track_count);toast_msg(msg);
}
static void set_page_list(ListKind kind,const char *title) {list_kind=kind;snprintf(page_title,sizeof(page_title),"%s",title);page=PAGE_LIST;rebuild_view();}
static void load_folder_entries(void) {
    folder_count=0;DIR *d=opendir(folder_current);if(!d)return;struct dirent *e;
    snprintf(folder_entries[folder_count++],MAX_PATH,"[ USE THIS FOLDER ]");
    snprintf(folder_entries[folder_count++],MAX_PATH,"[..]");
    while((e=readdir(d))&&folder_count<510){if(e->d_name[0]=='.')continue;char p[MAX_PATH];snprintf(p,sizeof(p),"%s/%s",folder_current,e->d_name);struct stat st;if(stat(p,&st)==0&&S_ISDIR(st.st_mode))snprintf(folder_entries[folder_count++],MAX_PATH,"%s/",e->d_name);}
    closedir(d);folder_selected=0;
}
static int create_playlist_file(const char *name) {
    char safe[80];int n=0;for(const char *p=name;*p&&n<70;p++)if((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p==' '||*p=='_'||*p=='-')safe[n++]=*p;safe[n]=0;
    while(n&&safe[n-1]==' ')safe[--n]=0;
    if(!n)return 0;
    char path[MAX_PATH];snprintf(path,sizeof(path),"%s/%s.m3u",playlist_dir,safe);FILE *f=fopen(path,"a");if(!f)return 0;fclose(f);snprintf(active_playlist,sizeof(active_playlist),"%s",path);return 1;
}
static void build_playlist_view(const char *file) {
    view_count=0;queue_count=0;FILE *f=fopen(file,"r");if(!f)return;char linebuf[MAX_PATH];while(fgets(linebuf,sizeof(linebuf),f)&&queue_count<MAX_TRACKS){linebuf[strcspn(linebuf,"\r\n")]=0;for(int i=0;i<track_count;i++)if(!strcmp(tracks[i].path,linebuf)){view_indices[view_count++]=i;queue_indices[queue_count++]=i;break;}}fclose(f);selected=0;scroll_pos=0;queue_pos=0;page=PAGE_LIST;snprintf(page_title,sizeof(page_title),"PLAYLIST");
}
static void render_background(void) {
    SDL_Color bg=rgb(12,12,18);if(page==PAGE_NOW){bg.r=(Uint8)(accent.r*0.18);bg.g=(Uint8)(accent.g*0.18);bg.b=(Uint8)(accent.b*0.18);}
    SDL_SetRenderDrawColor(renderer,bg.r,bg.g,bg.b,255);SDL_RenderClear(renderer);
    if(page==PAGE_NOW){for(int i=0;i<6;i++){int v=18-i*2;SDL_Color c=rgb((int)(bg.r+v),(int)(bg.g+v),(int)(bg.b+v));fill_rect(0,i*120,W,120,c);}}
}
static int read_int_file(const char *path,int *value) {
    FILE *f=fopen(path,"r");if(!f)return 0;int n=0;int ok=fscanf(f,"%d",&n)==1;fclose(f);if(ok&&value)*value=n;return ok;
}
static int read_text_file(const char *path,char *out,size_t cap) {
    if(!cap)return 0;
    FILE *f=fopen(path,"r");
    if(!f){out[0]=0;return 0;}
    if(!fgets(out,(int)cap,f)){fclose(f);out[0]=0;return 0;}
    fclose(f);
    out[strcspn(out,"\r\n")]=0;
    return 1;
}
static void poll_system_status(void) {
    Uint32 now=SDL_GetTicks();if(now-last_status_poll<1000)return;last_status_poll=now;
    system_battery=-1;system_charging=0;
    const char *battery_paths[]={"/sys/class/power_supply/BAT0/capacity","/sys/class/power_supply/battery/capacity","/sys/class/power_supply/BATTERY/capacity",NULL};
    for(int i=0;battery_paths[i];i++)if(read_int_file(battery_paths[i],&system_battery))break;
    if(system_battery<0){
        DIR *d=opendir("/sys/class/power_supply");if(d){struct dirent *e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char path[256],type[32];snprintf(path,sizeof(path),"/sys/class/power_supply/%s/type",e->d_name);if(read_text_file(path,type,sizeof(type))&&!strcasecmp(type,"Battery")){snprintf(path,sizeof(path),"/sys/class/power_supply/%s/capacity",e->d_name);if(read_int_file(path,&system_battery))break;}}closedir(d);}
    }
    if(system_battery>100)system_battery=100;
    if(system_battery<0)system_battery=-1;
    system_wifi=0;DIR *nd=opendir("/sys/class/net");if(nd){struct dirent *e;while((e=readdir(nd))){
        if(e->d_name[0]=='.'||!strcmp(e->d_name,"lo"))continue;
        char path[256],state[32];
        snprintf(path,sizeof(path),"/sys/class/net/%s/wireless",e->d_name);
        int is_wireless=(access(path,F_OK)==0 || !strncmp(e->d_name,"wlan",4) || !strncmp(e->d_name,"wl",2));
        if(!is_wireless)continue;
        snprintf(path,sizeof(path),"/sys/class/net/%s/operstate",e->d_name);
        if(read_text_file(path,state,sizeof(state))&&!strcmp(state,"up")){system_wifi=1;break;}
        int carrier=0;snprintf(path,sizeof(path),"/sys/class/net/%s/carrier",e->d_name);
        if(read_int_file(path,&carrier)&&carrier==1){system_wifi=1;break;}
    }closedir(nd);}
    const char *status_paths[]={"/sys/class/power_supply/BAT0/status","/sys/class/power_supply/battery/status","/sys/class/power_supply/BATTERY/status",NULL};char status[32]="";
    for(int i=0;status_paths[i];i++)if(read_text_file(status_paths[i],status,sizeof(status)))break;
    system_charging=(!strcasecmp(status,"Charging"));
}
static void draw_wifi_icon(int x,int y,SDL_Color c,int connected) {
    if(connected){line(x-8,y+2,x,y-5,c);line(x,y-5,x+8,y+2,c);line(x-5,y+2,x,y-2,c);line(x,y-2,x+5,y+2,c);circle(x,y+5,2,c,1);}else{line(x-7,y-4,x+7,y+5,c);line(x-7,y+5,x+7,y-4,c);}
}
static void draw_battery_icon(int x,int y,int percent,int charging,SDL_Color c) {
    SDL_Rect body={x,y,24,12};SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_RenderDrawRect(renderer,&body);fill_rect(x+24,y+4,3,4,c);
    if(percent>=0){int w=(22*percent)/100;if(w>0)fill_rect(x+1,y+1,w,10,charging?rgb(115,214,151):c);}else fill_rect(x+1,y+1,8,10,c);
    if(charging){line(x+12,y+1,x+9,y+6,rgb(15,15,20));line(x+9,y+6,x+14,y+6,rgb(15,15,20));line(x+14,y+6,x+11,y+11,rgb(15,15,20));}
}
static void render_status_bar(void) {
    poll_system_status();SDL_Color white=rgb(244,242,248),muted=rgb(185,180,192);time_t now=time(NULL);struct tm *tmv=localtime(&now);char clock_text[16]="--:--";if(tmv)strftime(clock_text,sizeof(clock_text),"%H:%M",tmv);
    text(clock_text,26,13,12,white,0);draw_wifi_icon(610,19,system_wifi?white:muted,system_wifi);draw_battery_icon(644,13,system_battery,system_charging,white);
    if(system_battery>=0){char pct[8];snprintf(pct,sizeof(pct),"%d%%",system_battery);text(pct,688,14,9,muted,1);}line(24,42,696,42,rgb(55,50,65));
}
static void render_top(const char *sub) {
    SDL_Color white=rgb(245,242,250),muted=rgb(160,154,174);render_status_bar();
    text(APP_NAME,28,54,15,white,0);if(sub)text(sub,690,57,11,muted,1);line(24,82,696,82,rgb(55,50,65));
}
static void render_button(int x,int y,const char *label,int selected_btn,int radius) {
    SDL_Color c=selected_btn?rgb(242,112,174):rgb(64,58,76);SDL_Color white=rgb(255,255,255);circle(x,y,radius,c,1);
    if(!strcmp(label,">")||!strcmp(label,"||")||!strcmp(label,"|<")||!strcmp(label,">|")){
        if(!strcmp(label,"||")){fill_rect(x-6,y-9,4,18,white);fill_rect(x+3,y-9,4,18,white);}
        else if(!strcmp(label,">")){line(x-5,y-10,x+8,y,white);line(x+8,y,x-5,y+10,white);line(x-5,y+10,x-5,y-10,white);}
        else if(!strcmp(label,"|<")){fill_rect(x-10,y-9,3,18,white);line(x+6,y-10,x-7,y,white);line(x-7,y,x+6,y+10,white);line(x+6,y+10,x+6,y-10,white);}
        else {fill_rect(x+7,y-9,3,18,white);line(x-6,y-10,x+7,y,white);line(x+7,y,x-6,y+10,white);line(x-6,y+10,x-6,y-10,white);}
    } else text(label,x,y-7,16,white,1);
}
static void draw_home_icon(int kind,int cx,int cy,SDL_Color c) {
    switch(kind){
        case 0: /* Recently added: clock */
            circle(cx,cy,13,c,0);line(cx,cy,cx,cy-8,c);line(cx,cy,cx+7,cy+3,c);break;
        case 1: /* Songs: music note */
            line(cx+5,cy-13,cx+5,cy+6,c);line(cx+5,cy-13,cx+14,cy-16,c);line(cx+14,cy-16,cx+14,cy+3,c);
            circle(cx+1,cy+7,5,c,1);circle(cx+10,cy+4,5,c,1);break;
        case 2: /* Artists: person */
            circle(cx,cy-6,5,c,0);line(cx-12,cy+12,cx-9,cy+4,c);line(cx-9,cy+4,cx,cy+1,c);line(cx,cy+1,cx+9,cy+4,c);line(cx+9,cy+4,cx+12,cy+12,c);break;
        case 3: /* Albums: record */
            circle(cx,cy,14,c,0);circle(cx,cy,5,c,0);circle(cx,cy,2,c,1);break;
        case 4: /* Playlists: ordered lines */
            for(int k=0;k<3;k++){fill_rect(cx-12,cy-10+k*10,3,3,c);line(cx-5,cy-8+k*10,cx+13,cy-8+k*10,c);}break;
        case 5: /* Folders */
            line(cx-14,cy-7,cx-14,cy+11,c);line(cx-14,cy+11,cx+14,cy+11,c);line(cx+14,cy+11,cx+14,cy-8,c);
            line(cx+14,cy-8,cx+2,cy-8,c);line(cx+2,cy-8,cx-2,cy-13,c);line(cx-2,cy-13,cx-14,cy-13,c);line(cx-14,cy-13,cx-14,cy-7,c);break;
        default: /* Settings: gear */
            circle(cx,cy,9,c,0);circle(cx,cy,3,c,0);
            for(int k=0;k<8;k++){double a=(double)k*0.78539816339;int x1=cx+(int)(10*cos(a)),y1=cy+(int)(10*sin(a));int x2=cx+(int)(14*cos(a)),y2=cy+(int)(14*sin(a));line(x1,y1,x2,y2,c);}break;
    }
}
static void render_home(void) {
    render_status_bar();SDL_Color white=rgb(247,244,250),muted=rgb(161,154,174);
    text("LISTEN NOW",30,63,28,white,0);text("YOUR MUSIC LIBRARY",31,105,10,muted,0);
    const char *names[]={"RECENTLY ADDED","SONGS","ARTISTS","ALBUMS","PLAYLISTS","FOLDERS","SETTINGS"};
    const char *desc[]={"Your newest music","All songs in your library","Browse your artists","Explore your albums","Your personal collections","Browse music folders","Customize Aurora"};
    const SDL_Color tile[]={ {118,77,146,255},{72,100,149,255},{153,91,126,255},{67,124,119,255},{151,107,65,255},{85,104,125,255},{93,88,112,255} };
    for(int i=0;i<7;i++){
        int y=133+i*71;if(i==selected)round_panel(24,y,672,62,rgb(47,40,57));
        round_panel(36,y+6,48,48,tile[i]);
        draw_home_icon(i,60,y+30,rgb(255,255,255));
        text(names[i],104,y+13,15,white,0);text(desc[i],104,y+36,9,muted,0);
        line(104,y+67,696,y+67,rgb(44,40,50));
        line(680,y+27,686,y+33,muted);line(686,y+33,680,y+39,muted);
    }
    if(current_track>=0&&current_track<track_count){
        int y=632;if(selected==7)round_panel(24,y,672,67,rgb(47,40,57));
        SDL_Texture *thumb=get_thumb(current_track);
        if(thumb){SDL_Rect td={38,y+8,50,50};SDL_RenderCopy(renderer,thumb,NULL,&td);}
        else round_panel(38,y+8,50,50,rgb(67,54,82));
        text("NOW PLAYING",104,y+13,9,rgb(242,151,190),0);
        text_width(tracks[current_track].title,104,y+29,13,white,470);
        text_width(tracks[current_track].album_artist,104,y+49,8,muted,470);
        circle(650,y+33,19,rgb(242,112,174),1);
        line(646,y+24,646,y+42,rgb(255,255,255));
        line(646,y+24,660,y+33,rgb(255,255,255));
        line(660,y+33,646,y+42,rgb(255,255,255));
    }
}
static void thumb_path_for(int idx, char *bmp, size_t cap) {
    unsigned long h=5381;for(const char *p=tracks[idx].path;*p;p++)h=((h<<5)+h)+(unsigned char)*p;
    snprintf(bmp,cap,"/tmp/aurora-r36/thumb-%lx.bmp",h);
}
static void extract_thumb(int idx) {
    if(idx<0||idx>=track_count)return;
    char bmp[MAX_PATH],q[MAX_PATH*2],cmd[4096];thumb_path_for(idx,bmp,sizeof(bmp));if(access(bmp,R_OK)==0)return;
    shell_quote(tracks[idx].path,q,sizeof(q));
    snprintf(cmd,sizeof(cmd),"ffmpeg -v error -y -i %s -map 0:v:0 -frames:v 1 -vf 'scale=72:72:force_original_aspect_ratio=decrease,pad=72:72:(ow-iw)/2:(oh-ih)/2:color=black' %s 2>/dev/null",q,bmp);
    if(system(cmd)!=0){
        char folder[MAX_PATH];snprintf(folder,sizeof(folder),"%s",tracks[idx].path);char *slash=strrchr(folder,'/');if(slash)*slash=0;
        const char *names[]={"cover.jpg","folder.jpg","Cover.jpg","Folder.jpg","cover.png","folder.png",NULL};
        for(int k=0;names[k];k++){char art[MAX_PATH];snprintf(art,sizeof(art),"%s/%s",folder,names[k]);if(access(art,R_OK)==0){shell_quote(art,q,sizeof(q));snprintf(cmd,sizeof(cmd),"ffmpeg -v error -y -i %s -vf 'scale=72:72:force_original_aspect_ratio=decrease,pad=72:72:(ow-iw)/2:(oh-ih)/2:color=black' %s 2>/dev/null",q,bmp);system(cmd);break;}}
    }
}
static void *thumb_worker_main(void *unused) {
    (void)unused;
    for(;;){pthread_mutex_lock(&thumb_mutex);while(thumb_qhead==thumb_qtail&&!thumb_worker_stop)pthread_cond_wait(&thumb_cond,&thumb_mutex);
        if(thumb_worker_stop){pthread_mutex_unlock(&thumb_mutex);break;}
        int idx=thumb_queue[thumb_qhead%MAX_TRACKS];thumb_qhead++;pthread_mutex_unlock(&thumb_mutex);
        extract_thumb(idx);
        pthread_mutex_lock(&thumb_mutex);if(idx>=0&&idx<MAX_TRACKS){thumb_queued[idx]=0;thumb_done[idx]=1;}pthread_mutex_unlock(&thumb_mutex);
    }
    return NULL;
}
static void queue_thumb(int idx) {
    if(idx<0||idx>=track_count)return;
    char bmp[MAX_PATH];thumb_path_for(idx,bmp,sizeof(bmp));if(access(bmp,R_OK)==0||thumb_done[idx])return;
    pthread_mutex_lock(&thumb_mutex);
    if(!thumb_queued[idx] && thumb_qtail-thumb_qhead<MAX_TRACKS-1){thumb_queue[thumb_qtail%MAX_TRACKS]=idx;thumb_qtail++;thumb_queued[idx]=1;pthread_cond_signal(&thumb_cond);}
    pthread_mutex_unlock(&thumb_mutex);
}
static SDL_Texture *get_thumb(int idx) {
    for(int i=0;i<12;i++)if(thumbs[i].track==idx&&thumbs[i].texture)return thumbs[i].texture;
    if(idx<0||idx>=track_count)return NULL;
    char bmp[MAX_PATH];thumb_path_for(idx,bmp,sizeof(bmp));
    if(access(bmp,R_OK)!=0){queue_thumb(idx);return NULL;}
    int slot=thumb_cursor++%12;if(thumbs[slot].texture){SDL_DestroyTexture(thumbs[slot].texture);thumbs[slot].texture=NULL;}
    thumbs[slot].track=idx;SDL_Surface *surface=SDL_LoadBMP(bmp);
    if(surface){thumbs[slot].texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_FreeSurface(surface);}
    return thumbs[slot].texture;
}

static void render_list(void) {
    render_top(page_title);SDL_Color white=rgb(246,243,250),muted=rgb(154,148,166);
    char info[128];snprintf(info,sizeof(info),"%d ITEMS   SORT: %s",view_count,sort_order);text(info,360,94,10,muted,1);
    int rows=10;if(selected<scroll_pos)scroll_pos=selected;if(selected>=scroll_pos+rows)scroll_pos=selected-rows+1;
    for(int r=0;r<rows&&scroll_pos+r<view_count;r++){
        int at=scroll_pos+r,idx=view_indices[at],y=120+r*50;if(idx<0||idx>=track_count)continue;Track *t=&tracks[idx];
        if(at==selected)round_panel(28,y,664,45,rgb(54,45,66));
        char title[MAX_TEXT+8],sub[160];snprintf(title,sizeof(title),"%s",t->title);
        if(list_kind==LIST_ARTISTS)snprintf(title,sizeof(title),"%s",t->album_artist);
        if(list_kind==LIST_ALBUMS)snprintf(title,sizeof(title),"%s",t->album);
        snprintf(sub,sizeof(sub),"%s  |  %s",t->album_artist,t->album);
        SDL_Texture *thumb=get_thumb(idx);if(thumb){SDL_Rect td={42,y+4,37,37};SDL_RenderCopy(renderer,thumb,NULL,&td);}else{round_panel(42,y+4,37,37,rgb(67,54,82));}
        text_width(title,96,y+7,16,white,505);text_width(sub,96,y+29,9,muted,505);
        char dur[24];int d=(int)t->duration;snprintf(dur,sizeof(dur),"%d:%02d",d/60,d%60);text(dur,654,y+16,10,muted,1);
    }
    text("A PLAY   B BACK   X SHUFFLE   Y REPEAT",360,672,9,muted,1);
}
static void render_now(void) {
    render_status_bar();SDL_Color white=rgb(250,249,250),muted=rgb(190,188,191),soft=rgb(145,143,148);
    if(current_track<0||current_track>=track_count){text("NOTHING PLAYING",360,340,24,white,1);text("CHOOSE A SONG FROM YOUR LIBRARY",360,380,10,muted,1);return;}
    Track *t=&tracks[current_track];
    if(cover_texture){SDL_Rect dst={148,64,424,424};SDL_RenderCopy(renderer,cover_texture,NULL,&dst);}
    else {round_panel(148,64,424,424,rgb(42,40,44));text("AURORA",360,270,32,rgb(220,218,222),1);}
    draw_text_raw(t->title,360,505,3,white,1,620);
    char meta[240];snprintf(meta,sizeof(meta),"%s | %s | %s",t->album_artist,t->album,t->year);text(meta,360,538,10,muted,1);
    int total=(int)(duration_sec>0?duration_sec:t->duration),pos=(int)position_sec;if(pos<0)pos=0;if(total<1)total=1;if(pos>total)pos=total;
    fill_rect(70,574,580,5,rgb(91,89,93));fill_rect(70,574,(580*pos)/total,5,rgb(242,240,244));
    char tm[32];snprintf(tm,sizeof(tm),"%d:%02d",pos/60,pos%60);text(tm,70,588,9,muted,0);snprintf(tm,sizeof(tm),"-%d:%02d",(total-pos)/60,(total-pos)%60);text(tm,650,588,9,muted,1);
    char tech[160];
    if(t->sample_rate[0]){double sr=atof(t->sample_rate)/1000.0;snprintf(tech,sizeof(tech),"%s | %s-bit | %.1f kHz",t->codec[0]?t->codec:"AUDIO",t->bit_depth[0]?t->bit_depth:"?",sr);}
    else snprintf(tech,sizeof(tech),"%s | %s-bit | SAMPLE RATE N/A",t->codec[0]?t->codec:"AUDIO",t->bit_depth[0]?t->bit_depth:"?");
    text(tech,360,615,9,soft,1);
    circle(126,671,22,shuffle_enabled?rgb(242,112,174):rgb(64,58,76),1);
    line(116,666,130,676,white);line(116,676,130,666,white);line(126,666,130,666,white);line(130,666,130,670,white);
    render_button(245,671,"|<",0,24);render_button(360,671,paused?">":"||",1,30);render_button(475,671,">|",0,24);
    circle(594,671,22,repeat_mode?rgb(242,112,174):rgb(64,58,76),1);
    line(585,670,585,664,white);line(585,664,590,660,white);line(590,660,600,660,white);line(600,660,605,665,white);line(605,665,600,665,white);
    char rep[8];snprintf(rep,sizeof(rep),"%s",repeat_mode==2?"1":repeat_mode==1?"ALL":"OFF");text(rep,594,679,6,white,1);
}
static void render_volume_overlay(void) {
    if(SDL_GetTicks()>=volume_overlay_until)return;
    int x=20,y=282;round_panel(x,y,82,156,rgb(35,32,42));
    /* Speaker glyph */
    fill_rect(x+16,y+26,8,14,rgb(245,244,248));line(x+24,y+26,x+36,y+17,rgb(245,244,248));line(x+24,y+40,x+36,y+49,rgb(245,244,248));
    int filled=(volume_overlay_value*86)/100;round_panel(x+25,y+62,30,92,rgb(83,78,91));if(filled>0)round_panel(x+28,y+65+(86-filled),24,filled,rgb(245,244,248));
    char pct[12];snprintf(pct,sizeof(pct),"%d%%",volume_overlay_value);text(pct,x+41,y+126,9,rgb(255,255,255),1);
}
static void render_seek_overlay(void) {
    if(SDL_GetTicks()>=seek_overlay_until)return;
    round_panel(231,72,258,42,rgb(43,39,51));text(seek_overlay_direction>0?"FORWARD SEEK":"REWIND SEEK",360,86,10,rgb(255,255,255),1);
}
static void render_settings(void) {
    render_top("SETTINGS");SDL_Color white=rgb(246,243,250),muted=rgb(162,154,176);char b[256];
    const char *labels[]={"MUSIC FOLDER","CROSSFADE","CROSSFADE MODE","CROSSFADE DURATION","SORT ORDER","RESCAN LIBRARY","VOLUME","JOYSTICK LED EFFECT","CHECK FOR UPDATES"};
    snprintf(b,sizeof(b),"%s",music_folder);
    for(int i=0;i<9;i++){int y=94+i*62;round_panel(34,y,652,54,selected==i?rgb(55,45,67):rgb(33,29,42));text(labels[i],56,y+10,13,white,0);
        if(i==0)text(b,642,y+19,8,muted,1);
        if(i==1)text(crossfade_enabled?"ON":"OFF",642,y+18,10,crossfade_enabled?rgb(247,128,177):muted,1);
        if(i==2){const char *m[]={"CROSSFADE BEATS","CROSSFADE VOCALS","NORMAL CROSSFADE","AUTO MIX"};text(m[crossfade_mode],642,y+18,8,muted,1);}
        if(i==3){snprintf(b,sizeof(b),"%d SEC",crossfade_seconds);text(b,642,y+18,10,muted,1);}
        if(i==4)text(sort_order,642,y+18,10,muted,1);
        if(i==5)text("SCAN",642,y+18,10,muted,1);
        if(i==6){snprintf(b,sizeof(b),"%d%%",volume);text(b,642,y+18,10,muted,1);}
        if(i==7){const char *m[]={"OFF","ALBUM TONE","LED WAVE"};const char *v=!led_available?"NOT AVAILABLE":(led_driver==2&&led_mode==2?"AMBIENT PULSE":m[led_mode]);text(v,642,y+18,8,led_available?rgb(242,155,190):muted,1);}
        if(i==8)text("CHECK",642,y+18,10,muted,1);
    }
    text("A CHANGE   LEFT / RIGHT ADJUST   B BACK",360,672,9,muted,1);
}
static void render_folders(void) {
    render_top("SELECT MUSIC FOLDER");SDL_Color white=rgb(244,240,248),muted=rgb(165,157,177);text(folder_current,360,82,12,muted,1);
    for(int i=0;i<10&&scroll_pos+i<folder_count;i++){int at=scroll_pos+i,y=122+i*47;if(at==folder_selected)round_panel(55,y,610,40,rgb(55,44,69));text(folder_entries[at],78,y+11,14,white,0);}
    text("A OPEN / SELECT    B BACK",360,665,10,muted,1);
}
static void render_playlist_names(void);
static void render(void) {
    render_background();if(page==PAGE_HOME)render_home();else if(page==PAGE_LIST)render_list();else if(page==PAGE_PLAYLISTS)render_playlist_names();else if(page==PAGE_NOW)render_now();else if(page==PAGE_SETTINGS)render_settings();else if(page==PAGE_FOLDERS)render_folders();
    if(page==PAGE_NOW){render_seek_overlay();render_volume_overlay();}
    if(toast[0]&&SDL_GetTicks()<toast_time){round_panel(150,48,420,42,rgb(65,52,80));text(toast,360,63,10,rgb(255,255,255),1);}
    SDL_RenderPresent(renderer);
}
static void folder_activate(void) {
    if(folder_selected==0){snprintf(music_folder,sizeof(music_folder),"%s",folder_current);save_config();page=PAGE_SETTINGS;selected=0;reload_library();return;}
    if(folder_selected==1){char *slash=strrchr(folder_current,'/');if(slash&&slash!=folder_current)*slash=0;else strcpy(folder_current,"/");load_folder_entries();scroll_pos=0;return;}
    const char *name=folder_entries[folder_selected];char p[MAX_PATH];snprintf(p,sizeof(p),"%s/%s",folder_current,name);p[strlen(p)-1]=0;if(chdir(p)==0){getcwd(folder_current,sizeof(folder_current));load_folder_entries();scroll_pos=0;}
}
static void open_playlists(void) {
    page=PAGE_PLAYLISTS;list_kind=LIST_PLAYLISTS;view_count=0;queue_count=0;selected=0;scroll_pos=0;DIR *d=opendir(playlist_dir);if(!d)return;struct dirent *e;
    while((e=readdir(d))&&view_count<MAX_TRACKS){size_t n=strlen(e->d_name);if(n<5||strcasecmp(e->d_name+n-4,".m3u"))continue;/* playlist entries encoded by pseudo index - handled separately */view_indices[view_count++]=-1;}
    closedir(d);selected=0;scroll_pos=0;
}
static void render_playlist_names(void) {
    render_top("PLAYLISTS");SDL_Color white=rgb(247,244,250),muted=rgb(160,153,174);DIR *d=opendir(playlist_dir);char names[512][MAX_PATH];int n=0;struct dirent *e;
    if(d){while((e=readdir(d))&&n<512){size_t z=strlen(e->d_name);if(z>4&&!strcasecmp(e->d_name+z-4,".m3u"))snprintf(names[n++],MAX_PATH,"%s",e->d_name);}closedir(d);}
    int total=n+1;for(int i=0;i<10&&scroll_pos+i<total;i++){int at=scroll_pos+i,y=126+i*51;if(at==selected)round_panel(36,y,648,45,rgb(55,44,69));if(at==0){circle(63,y+22,17,rgb(242,112,174),1);line(63,y+14,63,y+30,white);line(55,y+22,71,y+22,white);text("CREATE PLAYLIST",96,y+15,14,white,0);text("Start a new collection",96,y+34,8,muted,0);}else{char title[MAX_PATH];snprintf(title,sizeof(title),"%s",names[at-1]);title[strlen(title)-4]=0;text(title,72,y+16,14,white,0);}}
    text("A OPEN / CREATE   B BACK",360,672,9,muted,1);
}
static void change_volume(int delta) {
    volume+=delta;if(volume<0)volume=0;if(volume>100)volume=100;mpv_set_volume(volume);volume_overlay_value=volume;volume_overlay_until=SDL_GetTicks()+1450;
}
static void toggle_shuffle(void) {shuffle_enabled=!shuffle_enabled;save_config();toast_msg(shuffle_enabled?"SHUFFLE ON":"SHUFFLE OFF");}
static void cycle_repeat(void) {repeat_mode=(repeat_mode+1)%3;save_config();toast_msg(repeat_mode==0?"REPEAT OFF":repeat_mode==1?"REPEAT ALL":"REPEAT ONE");}
static int write_led_value(int value) {
    FILE *f=fopen("/sys/class/leds/joyled/brightness","w");
    if(!f)return 0;
    int ok=fprintf(f,"%d\n",value)>0;
    if(fclose(f)!=0)ok=0;
    return ok;
}
static int set_led_rgb(int r,int g,int b) {
    if(led_driver==1){
        /* ArkOS4Clone JoyLED v2 exposes named colour presets through brightness,
           not arbitrary RGB channels. Pick the closest documented static colour. */
        static const int palette[][4]={{255,0,0,20},{255,255,0,40},{0,255,0,60},{255,0,255,150},{0,255,255,170}};
        if(r==0&&g==0&&b==0)return write_led_value(0);
        double best=1.0e30;int value=20;
        for(size_t i=0;i<sizeof(palette)/sizeof(palette[0]);i++){
            double dr=(double)r-palette[i][0],dg=(double)g-palette[i][1],db=(double)b-palette[i][2];
            double d=dr*dr+dg*dg+db*db;if(d<best){best=d;value=palette[i][3];}
        }
        return write_led_value(value);
    }
    if(led_driver==2){
        char rgb_value[48];snprintf(rgb_value,sizeof(rgb_value),"%d %d %d",r,g,b);
        pid_t pid=fork();if(pid<0)return 0;
        if(pid==0){execlp("batocera-led-handheld","batocera-led-handheld","set_color_force_dec",rgb_value,(char*)NULL);_exit(127);}
        int status=0;if(waitpid(pid,&status,0)<0)return 0;
        return WIFEXITED(status)&&WEXITSTATUS(status)==0;
    }
    return 0;
}
static void detect_led_support(void) {
    led_driver=0;
    if(access("/sys/class/leds/joyled/brightness",W_OK)==0)led_driver=1;
    else if(access("/usr/bin/batocera-led-handheld",X_OK)==0||access("/usr/local/bin/batocera-led-handheld",X_OK)==0)led_driver=2;
    led_available=(led_driver!=0);
}
static void update_joystick_led(void) {
    if(!led_available)return;
    Uint32 now=SDL_GetTicks();if(now-last_led_poll<450)return;last_led_poll=now;
    if(led_mode==2&&led_driver==1){
        /* The supported JoyLED driver provides a built-in wave mode (brightness=200).
           It is generated by the LED controller and is not a live audio spectrum. */
        if(last_led_r!=-2){if(write_led_value(200)){last_led_r=-2;last_led_g=last_led_b=0;}}
        return;
    }
    int r=0,g=0,b=0;
    if(led_mode==0){r=g=b=0;}
    else if(led_mode==1){r=accent.r;g=accent.g;b=accent.b;}
    else {
        /* Batocera fallback: a low-cost ambience pulse, not an audio analyser. */
        double wave=(sin((double)now/260.0)+1.0)*0.5;double gain=0.28+0.72*wave;
        if(current_track<0||paused)gain=0.18;
        r=(int)(accent.r*gain);g=(int)(accent.g*gain);b=(int)(accent.b*gain);
    }
    if(r==last_led_r&&g==last_led_g&&b==last_led_b)return;
    if(set_led_rgb(r,g,b)){last_led_r=r;last_led_g=g;last_led_b=b;}
}
static void handle_settings(int key) {
    if(key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_RETURN||key==SDLK_z||key==SDLK_SPACE){int dir=(key==SDLK_LEFT)?-1:1;
        switch(selected){case 0:page=PAGE_FOLDERS;snprintf(folder_current,sizeof(folder_current),"%s",music_folder);load_folder_entries();scroll_pos=0;break;
        case 1:crossfade_enabled=!crossfade_enabled;break;
        case 2:crossfade_mode=(crossfade_mode+4+dir)%4;break;
        case 3:crossfade_seconds+=dir;if(crossfade_seconds<1)crossfade_seconds=12;if(crossfade_seconds>12)crossfade_seconds=1;break;
        case 4:{const char *s[]={"Name","Genre","Album Artist","Artist","Album","Year","Duration"};int i=0;for(;i<7;i++)if(!strcasecmp(sort_order,s[i]))break;i=(i+7+dir)%7;snprintf(sort_order,sizeof(sort_order),"%s",s[i]);rebuild_view();break;}
        case 5:reload_library();break;
        case 6:change_volume(dir*5);break;
        case 7:led_mode=(led_mode+3+dir)%3;if(!led_available)toast_msg("JOYSTICK LED NOT AVAILABLE ON THIS FIRMWARE");else{last_led_r=last_led_g=last_led_b=-1;update_joystick_led();}break;
        case 8:{const char *home="/roms/ports/Aurora-R36";char cmd[256];snprintf(cmd,sizeof(cmd),"AURORA_HOME='%s' sh '%s/update.sh' >/tmp/aurora-update.log 2>&1 &",home,home);int rc=system(cmd);toast_msg(rc==0?"OPENING UPDATE CHECK":"UPDATE LAUNCH FAILED");break;}}
        save_config();
    } else if(key==SDLK_UP){selected=(selected+8)%9;}else if(key==SDLK_DOWN){selected=(selected+1)%9;}
}
static void activate_selected(void) {
    if(page==PAGE_HOME){switch(selected){case 0:set_page_list(LIST_RECENT,"RECENTLY ADDED");break;case 1:set_page_list(LIST_ALL,"SONGS");break;case 2:set_page_list(LIST_ARTISTS,"ARTISTS");break;case 3:set_page_list(LIST_ALBUMS,"ALBUMS");break;case 4:open_playlists();break;case 5:page=PAGE_FOLDERS;snprintf(folder_current,sizeof(folder_current),"%s",music_folder);load_folder_entries();scroll_pos=0;break;case 6:page=PAGE_SETTINGS;selected=0;break;case 7:if(current_track>=0)page=PAGE_NOW;break;}return;}
    if(page==PAGE_LIST){if(selected<0||selected>=view_count)return;int idx=view_indices[selected];if(list_kind==LIST_ARTISTS||list_kind==LIST_ALBUMS){/* Filter list to selected group. */char key[256];snprintf(key,sizeof(key),"%s",list_kind==LIST_ARTISTS?tracks[idx].album_artist:tracks[idx].album);int n=0;for(int i=0;i<track_count;i++)if(!strcasecmp(list_kind==LIST_ARTISTS?tracks[i].album_artist:tracks[i].album,key))view_indices[n++]=i;view_count=n;list_kind=LIST_ALL;snprintf(page_title,sizeof(page_title),"%s",key);selected=0;scroll_pos=0;return;}queue_count=view_count;for(int i=0;i<view_count;i++)queue_indices[i]=view_indices[i];queue_pos=selected;play_index(idx);return;}
    if(page==PAGE_PLAYLISTS){if(selected==0){start_new_playlist();return;}DIR *d=opendir(playlist_dir);char names[512][MAX_PATH];int n=0;struct dirent *e;if(d){while((e=readdir(d))&&n<512){size_t z=strlen(e->d_name);if(z>4&&!strcasecmp(e->d_name+z-4,".m3u"))snprintf(names[n++],MAX_PATH,"%s",e->d_name);}closedir(d);}if(selected-1<n){char p[MAX_PATH];snprintf(p,sizeof(p),"%s/%s",playlist_dir,names[selected-1]);build_playlist_view(p);}return;}
}
static void start_new_playlist(void) {
    int n=1;char p[MAX_PATH];do{snprintf(playlist_name,sizeof(playlist_name),"PLAYLIST %02d",n++);snprintf(p,sizeof(p),"%s/%s.m3u",playlist_dir,playlist_name);}while(access(p,F_OK)==0&&n<1000);
    if(create_playlist_file(playlist_name))toast_msg("PLAYLIST CREATED");else toast_msg("PLAYLIST FAILED");
}
static int playlist_file_count(void) {int n=0;DIR *d=opendir(playlist_dir);if(!d)return 0;struct dirent *e;while((e=readdir(d))){size_t z=strlen(e->d_name);if(z>4&&!strcasecmp(e->d_name+z-4,".m3u"))n++;}closedir(d);return n;}
enum { DIR_UP=0, DIR_DOWN=1, DIR_LEFT=2, DIR_RIGHT=3 };
static void seek_by_seconds(int delta) {
    position_sec+=delta;if(position_sec<0)position_sec=0;if(duration_sec>0&&position_sec>duration_sec)position_sec=duration_sec;
    char c[128];snprintf(c,sizeof(c),"{\"command\":[\"seek\",%.1f,\"absolute\"]}",position_sec);mpv_cmd(c);seek_overlay_direction=delta>=0?1:-1;seek_overlay_until=SDL_GetTicks()+900;
}
static void direction_press(int dir) {
    if(dir<0||dir>3||held_dirs[dir])return;
    held_dirs[dir]=1;held_since[dir]=SDL_GetTicks();held_repeat[dir]=held_since[dir]+420;held_long[dir]=0;
    if(page==PAGE_NOW){if(dir==DIR_UP)change_volume(5);else if(dir==DIR_DOWN)change_volume(-5);return;}
    if(dir==DIR_UP||dir==DIR_DOWN)handle_key(dir==DIR_UP?SDLK_UP:SDLK_DOWN);
    else if(page==PAGE_SETTINGS)handle_settings(dir==DIR_LEFT?SDLK_LEFT:SDLK_RIGHT);
}
static void direction_release(int dir) {
    if(dir<0||dir>3||!held_dirs[dir])return;
    Uint32 elapsed=SDL_GetTicks()-held_since[dir];held_dirs[dir]=0;
    if(page==PAGE_NOW&&(dir==DIR_LEFT||dir==DIR_RIGHT)&&elapsed<420){play_next(dir==DIR_RIGHT?1:-1);return;}
}
static void process_held_directions(void) {
    Uint32 now=SDL_GetTicks();for(int d=0;d<4;d++)if(held_dirs[d]){
        if(page==PAGE_NOW){
            if(d==DIR_LEFT||d==DIR_RIGHT){if(now-held_since[d]>=420){if(!held_long[d]){held_long[d]=1;seek_by_seconds(d==DIR_RIGHT?5:-5);held_repeat[d]=now+170;}else if(now>=held_repeat[d]){seek_by_seconds(d==DIR_RIGHT?5:-5);held_repeat[d]=now+170;}}}
            else if(now>=held_repeat[d]){change_volume(d==DIR_UP?5:-5);held_repeat[d]=now+150;}
        } else if((d==DIR_UP||d==DIR_DOWN)&&now>=held_repeat[d]){handle_key(d==DIR_UP?SDLK_UP:SDLK_DOWN);held_repeat[d]=now+145;}
    }
}
static void quit_app(void){running=0;}
static void controller_button(int button,int down) {
    if(button==SDL_CONTROLLER_BUTTON_BACK){select_pressed=down;}
    if(button==SDL_CONTROLLER_BUTTON_START){start_pressed=down;}
    if(select_pressed&&start_pressed){quit_app();return;}
    if(button==SDL_CONTROLLER_BUTTON_DPAD_UP){if(down)direction_press(DIR_UP);else direction_release(DIR_UP);return;}
    if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){if(down)direction_press(DIR_DOWN);else direction_release(DIR_DOWN);return;}
    if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT){if(down)direction_press(DIR_LEFT);else direction_release(DIR_LEFT);return;}
    if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){if(down)direction_press(DIR_RIGHT);else direction_release(DIR_RIGHT);return;}
    if(!down)return;
    if(button==SDL_CONTROLLER_BUTTON_A){if(page==PAGE_NOW){mpv_cmd("{\"command\":[\"cycle\",\"pause\"]}");paused=!paused;}else if(page==PAGE_HOME||page==PAGE_LIST||page==PAGE_PLAYLISTS)activate_selected();else if(page==PAGE_FOLDERS)folder_activate();else if(page==PAGE_SETTINGS)handle_settings(SDLK_RETURN);}
    else if(button==SDL_CONTROLLER_BUTTON_B){if(page==PAGE_NOW){page=page_before_now;}else if(page==PAGE_FOLDERS||page==PAGE_SETTINGS||page==PAGE_LIST||page==PAGE_PLAYLISTS){page=PAGE_HOME;selected=0;} }
    else if(button==SDL_CONTROLLER_BUTTON_X)toggle_shuffle();
    else if(button==SDL_CONTROLLER_BUTTON_Y)cycle_repeat();
}
static void raw_button(int button,int down) {
    /* Common R36-family raw SDL joystick layout (used only when SDL has no
       GameController mapping): 0=B, 1=A, 2=X, 3=Y, 8..11=D-pad,
       12=Select, 13=Start. Shoulder buttons intentionally do nothing. */
    if(button>=8 && button<=11) {
        int dir=button-8;
        if(down) direction_press(dir); else direction_release(dir);
        return;
    }
    if(button==12){select_pressed=down;if(select_pressed&&start_pressed)quit_app();return;}
    if(button==13){start_pressed=down;if(select_pressed&&start_pressed)quit_app();return;}
    if(!down)return;
    if(button==0)controller_button(SDL_CONTROLLER_BUTTON_B,1);
    else if(button==1)controller_button(SDL_CONTROLLER_BUTTON_A,1);
    else if(button==2)controller_button(SDL_CONTROLLER_BUTTON_X,1);
    else if(button==3)controller_button(SDL_CONTROLLER_BUTTON_Y,1);
    /* L1/R1/L2/R2, stick-click and FN buttons intentionally have no action. */
}
static void axis_direction(int axis,int value) {
    if(page==PAGE_NOW || axis<0 || axis>=4)return;
    int dir=0;const int deadzone=10500;
    if(axis==SDL_CONTROLLER_AXIS_LEFTX||axis==SDL_CONTROLLER_AXIS_RIGHTX)dir=value<-deadzone?DIR_LEFT:value>deadzone?DIR_RIGHT:-1;
    else dir=value<-deadzone?DIR_UP:value>deadzone?DIR_DOWN:-1;
    int slot=axis;int old=stick_dirs[slot];if(dir<0){if(old){direction_release(old);stick_dirs[slot]=0;}return;}
    if(old!=dir){if(old)direction_release(old);stick_dirs[slot]=dir;direction_press(dir);stick_repeat[slot]=SDL_GetTicks()+260;}
    else if(SDL_GetTicks()>=stick_repeat[slot]){direction_release(dir);direction_press(dir);stick_repeat[slot]=SDL_GetTicks()+145;}
}
static void handle_hat(int value) {
    const int mask[4]={SDL_HAT_UP,SDL_HAT_DOWN,SDL_HAT_LEFT,SDL_HAT_RIGHT};
    for(int i=0;i<4;i++){int was=(hat_dirs&mask[i])!=0,now=(value&mask[i])!=0;if(now&&!was)direction_press(i);else if(was&&!now)direction_release(i);}
    hat_dirs=value;
}
static void handle_controller(const SDL_Event *e) {
    if(e->type==SDL_CONTROLLERBUTTONDOWN)controller_button(e->cbutton.button,1);
    else if(e->type==SDL_CONTROLLERBUTTONUP)controller_button(e->cbutton.button,0);
    else if(e->type==SDL_CONTROLLERAXISMOTION)axis_direction(e->caxis.axis,e->caxis.value);
    else if(e->type==SDL_JOYBUTTONDOWN&&!game_controller)raw_button(e->jbutton.button,1);
    else if(e->type==SDL_JOYBUTTONUP&&!game_controller)raw_button(e->jbutton.button,0);
    else if(e->type==SDL_JOYHATMOTION&&!game_controller)handle_hat(e->jhat.value);
    else if(e->type==SDL_JOYAXISMOTION&&!game_controller)axis_direction(e->jaxis.axis,e->jaxis.value);
}
static void handle_key(SDL_Keycode key) {
    if(text_entry){if(key==SDLK_ESCAPE){text_entry=0;return;}if(key==SDLK_RETURN){text_entry=0;if(create_playlist_file(playlist_name))toast_msg("PLAYLIST CREATED");return;}if(key==SDLK_BACKSPACE){size_t n=strlen(playlist_name);if(n)playlist_name[n-1]=0;}return;}
    if(page==PAGE_NOW){if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){page=page_before_now;return;}if(key==SDLK_SPACE||key==SDLK_RETURN){mpv_cmd("{\"command\":[\"cycle\",\"pause\"]}");paused=!paused;return;}if(key==SDLK_x)toggle_shuffle();if(key==SDLK_y)cycle_repeat();return;}
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){if(page==PAGE_HOME)quit_app();else{page=PAGE_HOME;selected=0;}return;}
    if(key==SDLK_F1){page=PAGE_SETTINGS;selected=0;return;}
    if(key==SDLK_v){if(current_track>=0)page=PAGE_NOW;return;}
    if(key==SDLK_UP){if(page==PAGE_SETTINGS)selected=(selected+8)%9;else if(page==PAGE_FOLDERS){if(folder_count>0)folder_selected=(folder_selected+folder_count-1)%folder_count;if(folder_selected<scroll_pos)scroll_pos=folder_selected;}else{int max=page==PAGE_HOME?(current_track>=0?8:7):page==PAGE_PLAYLISTS?playlist_file_count()+1:view_count;if(max>0)selected=(selected+max-1)%max;if(selected<scroll_pos)scroll_pos=selected;}return;}
    if(key==SDLK_DOWN){if(page==PAGE_SETTINGS)selected=(selected+1)%9;else if(page==PAGE_FOLDERS){if(folder_count>0)folder_selected=(folder_selected+1)%folder_count;if(folder_selected>=scroll_pos+10)scroll_pos=folder_selected-9;}else{int max=page==PAGE_HOME?(current_track>=0?8:7):page==PAGE_PLAYLISTS?playlist_file_count()+1:view_count;if(max>0)selected=(selected+1)%max;if(selected>=scroll_pos+10)scroll_pos=selected-9;}return;}
    if(page==PAGE_SETTINGS){if(key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_RETURN||key==SDLK_z||key==SDLK_SPACE)handle_settings(key);return;}
    if(page==PAGE_FOLDERS){if(key==SDLK_RETURN||key==SDLK_z)folder_activate();return;}
    if(key==SDLK_TAB){const char *sorts[]={"Name","Genre","Album Artist","Artist","Album","Year","Duration"};int i=0;for(;i<7;i++)if(!strcasecmp(sort_order,sorts[i]))break;snprintf(sort_order,sizeof(sort_order),"%s",sorts[(i+1)%7]);rebuild_view();return;}
    if(key==SDLK_x)toggle_shuffle();else if(key==SDLK_y)cycle_repeat();
    else if(key==SDLK_RETURN||key==SDLK_z||key==SDLK_SPACE){if(page==PAGE_HOME||page==PAGE_LIST||page==PAGE_PLAYLISTS)activate_selected();}
}
static void poll_player(void) {
    update_joystick_led();process_held_directions();
    static Uint32 last=0;Uint32 now=SDL_GetTicks();
    if(fade_stage){if(now-last>=45){last=now;update_fade();}return;}
    if(now-last<300)return;
    last=now;
    if(current_track>=0){position_sec=mpv_get_number("time-pos",position_sec);double d=mpv_get_number("duration",duration_sec);if(d>0)duration_sec=d;
        double eof=mpv_get_number("eof-reached",0);if(eof>0.5)play_next_automatic();
    }
}
static void cleanup(void) {
    save_config();if(mpv_pid>0){mpv_cmd("{\"command\":[\"quit\"]}");int st;for(int i=0;i<10;i++){if(waitpid(mpv_pid,&st,WNOHANG)==mpv_pid){mpv_pid=-1;break;}SDL_Delay(50);}if(mpv_pid>0){kill(mpv_pid,SIGTERM);waitpid(mpv_pid,NULL,0);}}
    if(thumb_worker_running){pthread_mutex_lock(&thumb_mutex);thumb_worker_stop=1;pthread_cond_broadcast(&thumb_cond);pthread_mutex_unlock(&thumb_mutex);pthread_join(thumb_worker,NULL);thumb_worker_running=0;}
    if(cover_texture)SDL_DestroyTexture(cover_texture);
    if(cover_surface)SDL_FreeSurface(cover_surface);
    for(int i=0;i<12;i++)if(thumbs[i].texture)SDL_DestroyTexture(thumbs[i].texture);
    if(game_controller)SDL_GameControllerClose(game_controller);else if(joystick)SDL_JoystickClose(joystick);
    if(renderer)SDL_DestroyRenderer(renderer);
    if(window)SDL_DestroyWindow(window);
    SDL_Quit();
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"--version")){printf("Aurora R36 Native C %s\n", APP_VERSION);return 0;}
    load_config();
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER)<0){fprintf(stderr,"SDL init failed: %s\n",SDL_GetError());return 1;}
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS,"0");
    Uint32 flags=SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_SHOWN; if(getenv("AURORA_WINDOW"))flags=SDL_WINDOW_SHOWN;
    window=SDL_CreateWindow(APP_NAME,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,W,H,flags);if(!window){fprintf(stderr,"SDL window failed: %s\n",SDL_GetError());SDL_Quit();return 1;}
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);if(!renderer){fprintf(stderr,"SDL renderer failed: %s\n",SDL_GetError());SDL_DestroyWindow(window);SDL_Quit();return 1;}
    if(pthread_create(&thumb_worker,NULL,thumb_worker_main,NULL)==0)thumb_worker_running=1;
    SDL_RenderSetLogicalSize(renderer,W,H);
    if(SDL_NumJoysticks()>0){if(SDL_IsGameController(0)){game_controller=SDL_GameControllerOpen(0);if(game_controller)joystick=SDL_GameControllerGetJoystick(game_controller);}if(!game_controller)joystick=SDL_JoystickOpen(0);}
    detect_led_support();
    if(!mpv_start())toast_msg("MPV NOT FOUND - INSTALL MPV");else mpv_set_volume(volume);
    reload_library();selected=0;page=PAGE_HOME;volume_overlay_value=volume;last_status_poll=0;update_joystick_led();Uint32 last_tick=SDL_GetTicks();
    while(running){SDL_Event e;while(SDL_PollEvent(&e)){
        if(e.type==SDL_QUIT)running=0;
        else if(e.type==SDL_KEYDOWN&&!e.key.repeat){SDL_Keycode k=e.key.keysym.sym;if(k==SDLK_UP)direction_press(DIR_UP);else if(k==SDLK_DOWN)direction_press(DIR_DOWN);else if(k==SDLK_LEFT)direction_press(DIR_LEFT);else if(k==SDLK_RIGHT)direction_press(DIR_RIGHT);else handle_key(k);}
        else if(e.type==SDL_KEYUP){SDL_Keycode k=e.key.keysym.sym;if(k==SDLK_UP)direction_release(DIR_UP);else if(k==SDLK_DOWN)direction_release(DIR_DOWN);else if(k==SDLK_LEFT)direction_release(DIR_LEFT);else if(k==SDLK_RIGHT)direction_release(DIR_RIGHT);}
        else if(e.type==SDL_TEXTINPUT&&text_entry){size_t n=strlen(playlist_name);if(n+strlen(e.text.text)<sizeof(playlist_name)-1)strcat(playlist_name,e.text.text);}
        else if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP||e.type==SDL_CONTROLLERAXISMOTION||e.type==SDL_JOYBUTTONDOWN||e.type==SDL_JOYBUTTONUP||e.type==SDL_JOYHATMOTION||e.type==SDL_JOYAXISMOTION)handle_controller(&e);
    }
        if(page==PAGE_PLAYLISTS){/* list view is refreshed in renderer */}
        poll_player();if(SDL_GetTicks()-last_tick>16){render();last_tick=SDL_GetTicks();}SDL_Delay(2);
    }
    cleanup();return 0;
}
