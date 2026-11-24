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

#define APP_NAME "AURORA R36"
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

typedef enum { PAGE_HOME, PAGE_LIST, PAGE_NOW, PAGE_SETTINGS, PAGE_FOLDERS, PAGE_PLAYLISTS, PAGE_BUTTON_SETUP } Page;
typedef struct { int kind; int code; } ButtonBinding; /* 1=joystick button, 2=hat direction */
typedef enum { LIST_ALL, LIST_ARTISTS, LIST_ALBUMS, LIST_PLAYLISTS } ListKind;

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
static int pending_create_playlist = 0;
static char playlist_name[80] = "";
static int text_entry = 0;
static ButtonBinding button_map[8] = {{2,SDL_HAT_UP},{2,SDL_HAT_DOWN},{2,SDL_HAT_LEFT},{2,SDL_HAT_RIGHT},{1,0},{1,1},{1,4},{1,5}};
static const char *button_names[8] = {"UP","DOWN","LEFT","RIGHT","CONFIRM","BACK","NEXT","PREVIOUS"};
static int setup_step = 0;
static int button_setup_done = 0;
static int setup_active = 0;
typedef struct { int track; SDL_Texture *texture; } Thumb;
static Thumb thumbs[12];
static int thumb_cursor = 0;
static pthread_t thumb_worker;
static pthread_mutex_t thumb_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t thumb_cond = PTHREAD_COND_INITIALIZER;
static int thumb_worker_running = 0, thumb_worker_stop = 0;
static int thumb_queue[MAX_TRACKS], thumb_qhead = 0, thumb_qtail = 0;
static unsigned char thumb_queued[MAX_TRACKS], thumb_done[MAX_TRACKS];

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
    if(list_kind==LIST_ALL){for(int i=0;i<track_count;i++)view_indices[view_count++]=i;}
    else if(list_kind==LIST_ARTISTS){
        for(int i=0;i<track_count;i++){int seen=0;for(int j=0;j<view_count;j++)if(!strcasecmp(tracks[i].album_artist,tracks[view_indices[j]].album_artist)){seen=1;break;}if(!seen)view_indices[view_count++]=i;}
    } else if(list_kind==LIST_ALBUMS){
        for(int i=0;i<track_count;i++){int seen=0;for(int j=0;j<view_count;j++)if(!strcasecmp(tracks[i].album,tracks[view_indices[j]].album)&&!strcasecmp(tracks[i].album_artist,tracks[view_indices[j]].album_artist)){seen=1;break;}if(!seen)view_indices[view_count++]=i;}
    }
    for(int i=0;i<view_count;i++)for(int j=i+1;j<view_count;j++){
        int cmp=0;
        if(list_kind==LIST_ARTISTS)cmp=strcasecmp(tracks[view_indices[i]].album_artist,tracks[view_indices[j]].album_artist);
        else if(list_kind==LIST_ALBUMS)cmp=strcasecmp(tracks[view_indices[i]].album,tracks[view_indices[j]].album);
        else cmp=cmp_field(&tracks[view_indices[i]],&tracks[view_indices[j]]);
        if(cmp>0){int x=view_indices[i];view_indices[i]=view_indices[j];view_indices[j]=x;}
    }
    selected=0;scroll_pos=0;
}
static void save_config(void) {
    char p[MAX_PATH];snprintf(p,sizeof(p),"%s/settings.conf",config_dir);FILE *f=fopen(p,"w");if(!f)return;
    fprintf(f,"music_folder=%s\nvolume=%d\nsort_order=%s\ncrossfade_enabled=%d\ncrossfade_mode=%d\ncrossfade_seconds=%d\nbutton_setup_done=%d\n",music_folder,volume,sort_order,crossfade_enabled,crossfade_mode,crossfade_seconds,button_setup_done);
    for(int i=0;i<8;i++)fprintf(f,"button_%d=%d,%d\n",i,button_map[i].kind,button_map[i].code);
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
        else if(!strcmp(ln,"button_setup_done"))button_setup_done=atoi(eq);
        else if(!strncmp(ln,"button_",7)){int i=atoi(ln+7),kind=0,code=0;if(i>=0&&i<8&&sscanf(eq,"%d,%d",&kind,&code)==2){button_map[i].kind=kind;button_map[i].code=code;}}
    }fclose(f);
    if(volume<0)volume=0;
    if(volume>100)volume=100;
    if(crossfade_seconds<1)crossfade_seconds=1;
    if(crossfade_seconds>12)crossfade_seconds=12;
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
    transition_target=-1;fade_stage=0;current_track=idx;position_sec=0;duration_sec=tracks[idx].duration;cache_cover(idx);mpv_play_path(tracks[idx].path);page=PAGE_NOW;
    if(fade_in&&crossfade_enabled){mpv_set_volume(0);fade_stage=2;fade_started=SDL_GetTicks();fade_start_value=0;}
    else mpv_set_volume(volume);
}
static void play_index(int idx) {
    if(idx<0||idx>=track_count)return;
    if(crossfade_enabled&&current_track>=0&&current_track!=idx){
        transition_target=idx;fade_stage=1;fade_started=SDL_GetTicks();fade_start_value=actual_volume;page=PAGE_NOW;return;
    }
    start_track_now(idx,crossfade_enabled&&current_track<0);
}
static void play_next(int dir) {
    int next=-1;
    if(queue_count>0){queue_pos+=dir;if(queue_pos<0)queue_pos=queue_count-1;if(queue_pos>=queue_count)queue_pos=0;next=queue_indices[queue_pos];}
    else if(track_count){int i=current_track<0?0:current_track+dir;if(i<0)i=track_count-1;if(i>=track_count)i=0;next=i;}
    if(next>=0)play_index(next);
}
static void play_next_automatic(void) {
    int next=-1;
    if(queue_count>0){queue_pos++;if(queue_pos>=queue_count)queue_pos=0;next=queue_indices[queue_pos];}
    else if(track_count){int i=current_track+1;if(i>=track_count)i=0;next=i;}
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
static void add_to_playlist(int idx) {
    if(idx<0||idx>=track_count)return;
    if(!active_playlist[0]){char p[MAX_PATH];snprintf(p,sizeof(p),"%s/Favorites.m3u",playlist_dir);snprintf(active_playlist,sizeof(active_playlist),"%s",p);}
    FILE *f=fopen(active_playlist,"a");if(!f){toast_msg("CANNOT WRITE PLAYLIST");return;}fprintf(f,"%s\n",tracks[idx].path);fclose(f);toast_msg("ADDED TO PLAYLIST");
}
static void build_playlist_view(const char *file) {
    view_count=0;queue_count=0;FILE *f=fopen(file,"r");if(!f)return;char linebuf[MAX_PATH];while(fgets(linebuf,sizeof(linebuf),f)&&queue_count<MAX_TRACKS){linebuf[strcspn(linebuf,"\r\n")]=0;for(int i=0;i<track_count;i++)if(!strcmp(tracks[i].path,linebuf)){view_indices[view_count++]=i;queue_indices[queue_count++]=i;break;}}fclose(f);selected=0;scroll_pos=0;queue_pos=0;page=PAGE_LIST;snprintf(page_title,sizeof(page_title),"PLAYLIST");
}
static void render_background(void) {
    SDL_Color bg=rgb(12,12,18);if(page==PAGE_NOW){bg.r=(Uint8)(accent.r*0.18);bg.g=(Uint8)(accent.g*0.18);bg.b=(Uint8)(accent.b*0.18);}
    SDL_SetRenderDrawColor(renderer,bg.r,bg.g,bg.b,255);SDL_RenderClear(renderer);
    if(page==PAGE_NOW){for(int i=0;i<6;i++){int v=18-i*2;SDL_Color c=rgb((int)(bg.r+v),(int)(bg.g+v),(int)(bg.b+v));fill_rect(0,i*120,W,120,c);}}
}
static void render_top(const char *sub) {
    SDL_Color white=rgb(245,242,250),muted=rgb(160,154,174),pink=rgb(242,116,173);
    text(APP_NAME,32,24,16,white,0);text(sub,688,28,12,muted,1);line(32,60,688,60,rgb(55,50,65));
    (void)pink;
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
static void render_home(void) {
    render_top("OFFLINE MUSIC");SDL_Color white=rgb(247,244,250),muted=rgb(161,154,174);
    text("YOUR MUSIC, YOUR WAY",360,102,24,white,1);text("AURORA",360,160,46,white,1);
    const char *names[]={"ALL SONGS","ARTISTS","ALBUMS","PLAYLISTS","SETTINGS"};
    const char *desc[]={"EVERY TRACK IN YOUR LIBRARY","BROWSE BY ALBUM ARTIST","EXPLORE YOUR ALBUMS","YOUR CUSTOM COLLECTIONS","PLAYBACK AND LIBRARY"};
    for(int i=0;i<5;i++){int y=238+i*82;round_panel(76,y,568,66,rgb(34,30,43));text(names[i],110,y+15,20,white,0);text(desc[i],610,y+22,10,muted,1);if(i==selected)round_panel(76,y,5,66,rgb(242,112,174));}
    text("A SELECT     X ADD TO PLAYLIST     Y NOW PLAYING",360,666,10,muted,1);
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
    char info[128];snprintf(info,sizeof(info),"%d ITEMS   SORT: %s",view_count,sort_order);text(info,360,82,11,muted,1);
    int rows=10;if(selected<scroll_pos)scroll_pos=selected;if(selected>=scroll_pos+rows)scroll_pos=selected-rows+1;
    for(int r=0;r<rows&&scroll_pos+r<view_count;r++){
        int at=scroll_pos+r,idx=view_indices[at],y=116+r*49;Track *t=&tracks[idx];
        if(at==selected)round_panel(38,y,644,43,rgb(54,45,66));
        char title[MAX_TEXT+8],sub[160];snprintf(title,sizeof(title),"%s",t->title);
        if(list_kind==LIST_ARTISTS)snprintf(title,sizeof(title),"%s",t->album_artist);
        if(list_kind==LIST_ALBUMS)snprintf(title,sizeof(title),"%s",t->album);
        snprintf(sub,sizeof(sub),"%s  |  %s",t->album_artist,t->album);
        SDL_Texture *thumb=get_thumb(idx);if(thumb){SDL_Rect td={53,y+4,35,35};SDL_RenderCopy(renderer,thumb,NULL,&td);}else{round_panel(53,y+4,35,35,rgb(67,54,82));}
        text_width(title,101,y+7,16,white,500);text_width(sub,101,y+29,9,muted,500);
        char dur[24];int d=(int)t->duration;snprintf(dur,sizeof(dur),"%d:%02d",d/60,d%60);text(dur,652,y+15,10,muted,1);
    }
    text("A PLAY / OPEN    X ADD    L1/R1 SORT    B BACK",360,664,10,muted,1);
}
static void render_now(void) {
    SDL_Color white=rgb(250,249,250),muted=rgb(190,188,191),soft=rgb(145,143,148);
    if(current_track<0||current_track>=track_count){text("NOTHING PLAYING",360,340,24,white,1);text("CHOOSE A SONG FROM YOUR LIBRARY",360,380,10,muted,1);return;}
    Track *t=&tracks[current_track];
    /* Square artwork dominates the screen, matching the supplied compact player reference. */
    if(cover_texture){SDL_Rect dst={112,24,496,496};SDL_RenderCopy(renderer,cover_texture,NULL,&dst);}
    else {round_panel(112,24,496,496,rgb(42,40,44));text("AURORA",360,258,32,rgb(220,218,222),1);}
    /* Centered title and metadata; the overflow control replaces the former favorite button. */
    draw_text_raw(t->title,360,535,3,white,1,540);
    char meta[220];snprintf(meta,sizeof(meta),"%s | %s | %s",t->album_artist,t->album,t->year);text(meta,360,572,11,muted,1);
    /* Three-dot menu control, deliberately no star/favorite icon. */
    circle(650,548,23,rgb(65,63,68),1);
    circle(642,548,2,white,1);circle(650,548,2,white,1);circle(658,548,2,white,1);
    int total=(int)(duration_sec>0?duration_sec:t->duration),pos=(int)position_sec;if(pos<0)pos=0;if(total<1)total=1;if(pos>total)pos=total;
    fill_rect(84,615,552,6,rgb(91,89,93));fill_rect(84,615,(552*pos)/total,6,rgb(242,240,244));
    char tm[32];snprintf(tm,sizeof(tm),"%d:%02d",pos/60,pos%60);text(tm,84,635,9,muted,0);
    snprintf(tm,sizeof(tm),"-%d:%02d",(total-pos)/60,(total-pos)%60);text(tm,636,635,9,muted,1);
    char tech[160];double sr=atof(t->sample_rate)/1000.0;
    snprintf(tech,sizeof(tech),"%s  |  %s-bit  |  %.1f kHz",t->codec[0]?t->codec:"AUDIO",t->bit_depth[0]?t->bit_depth:"?",sr);text(tech,360,635,9,soft,1);
    /* Shuffle and repeat flank the transport controls, as in the reference layout. */
    line(101,672,123,688,white);line(101,688,123,672,white);line(118,672,123,672,white);line(123,672,123,677,white);line(101,683,101,688,white);line(101,688,106,688,white);
    render_button(245,680,"|<",0,24);render_button(360,680,paused?">":"||",1,30);render_button(475,680,">|",0,24);
    circle(608,680,23,rgb(65,63,68),1);line(596,679,596,673,white);line(596,673,601,668,white);line(601,668,615,668,white);line(615,668,619,673,white);line(619,673,614,673,white);line(619,681,619,687,white);line(619,687,614,692,white);line(614,692,600,692,white);line(600,692,596,687,white);line(596,687,601,687,white);
}
static void render_button_setup(void) {
    render_top("CONTROLLER SETUP");
    SDL_Color white=rgb(248,246,250),muted=rgb(178,171,184);
    text("MAP YOUR CONTROLS",360,150,25,white,1);
    if(setup_step<8){
        char line1[96];snprintf(line1,sizeof(line1),"PRESS THE CONTROL FOR: %s",button_names[setup_step]);
        text(line1,360,290,17,rgb(246,145,190),1);
        text("PRESS A D-PAD DIRECTION OR A BUTTON",360,340,11,muted,1);
        text("B / ESC CANCELS SETUP",360,650,10,muted,1);
        char count[32];snprintf(count,sizeof(count),"%d OF 8",setup_step+1);text(count,360,390,12,white,1);
    } else {
        text("BUTTON MAPPING COMPLETE",360,310,20,white,1);
        text("PRESS CONFIRM TO SAVE",360,355,12,muted,1);
    }
}
static void render_settings(void) {
    render_top("SETTINGS");SDL_Color white=rgb(246,243,250),muted=rgb(162,154,176);char b[256];
    const char *labels[]={"MUSIC FOLDER","CROSSFADE","CROSSFADE MODE","CROSSFADE DURATION","SORT ORDER","RESCAN LIBRARY","VOLUME","BUTTON SETUP","CHECK FOR UPDATES"};
    snprintf(b,sizeof(b),"%s",music_folder);
    for(int i=0;i<9;i++){int y=92+i*64;round_panel(50,y,620,56,selected==i?rgb(55,45,67):rgb(33,29,42));text(labels[i],74,y+10,14,white,0);
        if(i==0)text(b,630,y+20,9,muted,1);
        if(i==1)text(crossfade_enabled?"ON":"OFF",630,y+20,12,crossfade_enabled?rgb(247,128,177):muted,1);
        if(i==2){const char *m[]={"CROSSFADE BEATS","CROSSFADE VOCALS","NORMAL CROSSFADE","AUTO MIX"};text(m[crossfade_mode],630,y+20,9,muted,1);}
        if(i==3){snprintf(b,sizeof(b),"%d SEC",crossfade_seconds);text(b,630,y+20,11,muted,1);}
        if(i==4)text(sort_order,630,y+20,11,muted,1);
        if(i==5)text("SCAN",630,y+20,11,muted,1);
        if(i==6){snprintf(b,sizeof(b),"%d%%",volume);text(b,630,y+20,11,muted,1);}
        if(i==7)text("CONFIGURE",630,y+20,10,muted,1);
        if(i==8)text("CHECK",630,y+20,10,muted,1);
    }
    text("A CHANGE    LEFT / RIGHT ADJUST    B BACK",360,665,10,muted,1);
}
static void render_folders(void) {
    render_top("SELECT MUSIC FOLDER");SDL_Color white=rgb(244,240,248),muted=rgb(165,157,177);text(folder_current,360,82,12,muted,1);
    for(int i=0;i<10&&scroll_pos+i<folder_count;i++){int at=scroll_pos+i,y=122+i*47;if(at==folder_selected)round_panel(55,y,610,40,rgb(55,44,69));text(folder_entries[at],78,y+11,14,white,0);}
    text("A OPEN / SELECT    B BACK",360,665,10,muted,1);
}
static void render_playlist_names(void);
static void render(void) {
    render_background();if(page==PAGE_HOME)render_home();else if(page==PAGE_LIST||page==PAGE_PLAYLISTS)render_list();else if(page==PAGE_NOW)render_now();else if(page==PAGE_SETTINGS)render_settings();else if(page==PAGE_FOLDERS)render_folders();else if(page==PAGE_BUTTON_SETUP)render_button_setup();
    if(toast[0]&&SDL_GetTicks()<toast_time){round_panel(150,24,420,42,rgb(65,52,80));text(toast,360,40,10,rgb(255,255,255),1);}
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
    for(int i=0;i<10&&scroll_pos+i<n;i++){int at=scroll_pos+i,y=130+i*49;char title[MAX_PATH];snprintf(title,sizeof(title),"%s",names[at]);title[strlen(title)-4]=0;if(at==selected)round_panel(48,y,624,42,rgb(55,44,69));text(title,72,y+13,15,white,0);}
    text("A OPEN    X NEW PLAYLIST    B BACK",360,665,10,muted,1);
}
static void handle_settings(int key) {
    if(key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_RETURN||key==SDLK_z||key==SDLK_SPACE){int dir=(key==SDLK_LEFT)?-1:1;
        switch(selected){case 0:page=PAGE_FOLDERS;snprintf(folder_current,sizeof(folder_current),"%s",music_folder);load_folder_entries();scroll_pos=0;break;
        case 1:crossfade_enabled=!crossfade_enabled;break;
        case 2:crossfade_mode=(crossfade_mode+4+dir)%4;break;
        case 3:crossfade_seconds+=dir;if(crossfade_seconds<1)crossfade_seconds=12;if(crossfade_seconds>12)crossfade_seconds=1;break;
        case 4:{const char *s[]={"Name","Genre","Album Artist","Artist","Album","Year","Duration"};int i=0;for(;i<7;i++)if(!strcasecmp(sort_order,s[i]))break;i=(i+7+dir)%7;snprintf(sort_order,sizeof(sort_order),"%s",s[i]);rebuild_view();break;}
        case 5:reload_library();break;
        case 6:volume+=dir*5;if(volume<0)volume=0;if(volume>100)volume=100;mpv_set_volume(volume);break;
        case 7:page=PAGE_BUTTON_SETUP;setup_step=0;setup_active=1;button_setup_done=0;toast_msg("MAP CONTROLS");break;
        case 8:{const char *home="/roms/ports/Aurora-R36";char cmd[256];snprintf(cmd,sizeof(cmd),"AURORA_HOME='%s' sh '%s/update.sh' >/tmp/aurora-update.log 2>&1 &",home,home);int rc=system(cmd);toast_msg(rc==0?"OPENING UPDATE CHECK":"UPDATE LAUNCH FAILED");break;}}
        save_config();
    } else if(key==SDLK_UP){selected=(selected+8)%9;}else if(key==SDLK_DOWN){selected=(selected+1)%9;}
}
static void activate_selected(void) {
    if(page==PAGE_HOME){switch(selected){case 0:set_page_list(LIST_ALL,"ALL SONGS");break;case 1:set_page_list(LIST_ARTISTS,"ARTISTS");break;case 2:set_page_list(LIST_ALBUMS,"ALBUMS");break;case 3:open_playlists();break;case 4:page=PAGE_SETTINGS;selected=0;break;}return;}
    if(page==PAGE_LIST){if(selected<0||selected>=view_count)return;int idx=view_indices[selected];if(list_kind==LIST_ARTISTS||list_kind==LIST_ALBUMS){/* Filter list to selected group. */char key[256];snprintf(key,sizeof(key),"%s",list_kind==LIST_ARTISTS?tracks[idx].album_artist:tracks[idx].album);int n=0;for(int i=0;i<track_count;i++)if(!strcasecmp(list_kind==LIST_ARTISTS?tracks[i].album_artist:tracks[i].album,key))view_indices[n++]=i;view_count=n;list_kind=LIST_ALL;snprintf(page_title,sizeof(page_title),"%s",key);selected=0;scroll_pos=0;return;}queue_count=view_count;for(int i=0;i<view_count;i++)queue_indices[i]=view_indices[i];queue_pos=selected;play_index(idx);return;}
    if(page==PAGE_PLAYLISTS){DIR *d=opendir(playlist_dir);char names[512][MAX_PATH];int n=0;struct dirent *e;if(d){while((e=readdir(d))&&n<512){size_t z=strlen(e->d_name);if(z>4&&!strcasecmp(e->d_name+z-4,".m3u"))snprintf(names[n++],MAX_PATH,"%s",e->d_name);}closedir(d);}if(selected<n){char p[MAX_PATH];snprintf(p,sizeof(p),"%s/%s",playlist_dir,names[selected]);build_playlist_view(p);}return;}
}
static void start_new_playlist(void) {
    int n=1;char p[MAX_PATH];do{snprintf(playlist_name,sizeof(playlist_name),"PLAYLIST %02d",n++);snprintf(p,sizeof(p),"%s/%s.m3u",playlist_dir,playlist_name);}while(access(p,F_OK)==0&&n<1000);
    if(create_playlist_file(playlist_name))toast_msg("PLAYLIST CREATED");else toast_msg("PLAYLIST FAILED");
}
static int playlist_file_count(void) {int n=0;DIR *d=opendir(playlist_dir);if(!d)return 0;struct dirent *e;while((e=readdir(d))){size_t z=strlen(e->d_name);if(z>4&&!strcasecmp(e->d_name+z-4,".m3u"))n++;}closedir(d);return n;}
static void handle_key(SDL_Keycode key) {
    if(text_entry){if(key==SDLK_ESCAPE){text_entry=0;return;}if(key==SDLK_RETURN){text_entry=0;if(create_playlist_file(playlist_name))toast_msg("PLAYLIST CREATED");return;}if(key==SDLK_BACKSPACE){size_t n=strlen(playlist_name);if(n)playlist_name[n-1]=0;}return;}
    if(page==PAGE_BUTTON_SETUP){if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){page=PAGE_SETTINGS;setup_active=0;return;}return;}
    if(page==PAGE_NOW){if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){page=PAGE_HOME;return;}if(key==SDLK_LEFT){play_next(-1);return;}if(key==SDLK_RIGHT){play_next(1);return;}if(key==SDLK_SPACE||key==SDLK_RETURN){mpv_cmd("{\"command\":[\"cycle\",\"pause\"]}");paused=!paused;return;}if(key==SDLK_UP){char c[128];position_sec-=10;if(position_sec<0)position_sec=0;snprintf(c,sizeof(c),"{\"command\":[\"seek\",%.1f,\"absolute\"]}",position_sec);mpv_cmd(c);return;}if(key==SDLK_DOWN){char c[128];position_sec+=10;snprintf(c,sizeof(c),"{\"command\":[\"seek\",%.1f,\"absolute\"]}",position_sec);mpv_cmd(c);return;}return;}
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){if(page==PAGE_FOLDERS)page=PAGE_SETTINGS;else page=PAGE_HOME;selected=0;return;}
    if(key==SDLK_F1){page=PAGE_SETTINGS;selected=0;return;}
    if(key==SDLK_v){page=PAGE_NOW;return;}
    if(key==SDLK_UP){if(page==PAGE_SETTINGS){selected=(selected+8)%9;}else if(page==PAGE_FOLDERS){folder_selected=(folder_selected+folder_count-1)%folder_count;if(folder_selected<scroll_pos)scroll_pos=folder_selected;}else{int max=page==PAGE_HOME?5:page==PAGE_PLAYLISTS?playlist_file_count():view_count;if(max>0)selected=(selected+max-1)%max;if(selected<scroll_pos)scroll_pos=selected;}return;}
    if(key==SDLK_DOWN){if(page==PAGE_SETTINGS){selected=(selected+1)%9;}else if(page==PAGE_FOLDERS){folder_selected=(folder_selected+1)%folder_count;if(folder_selected>=scroll_pos+10)scroll_pos=folder_selected-9;}else{int max=page==PAGE_HOME?5:page==PAGE_PLAYLISTS?playlist_file_count():view_count;if(max>0)selected=(selected+1)%max;if(selected>=scroll_pos+10)scroll_pos=selected-9;}return;}
    if(page==PAGE_SETTINGS){if(key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_RETURN||key==SDLK_z||key==SDLK_SPACE)handle_settings(key);return;}
    if(page==PAGE_FOLDERS){if(key==SDLK_RETURN||key==SDLK_z)folder_activate();return;}
    if(key==SDLK_TAB){const char *s[]={"Name","Genre","Album Artist","Artist","Album","Year","Duration"};int i=0;for(;i<7;i++)if(!strcasecmp(sort_order,s[i]))break;snprintf(sort_order,sizeof(sort_order),"%s",s[(i+1)%7]);rebuild_view();return;}
    if(key==SDLK_x||key==SDLK_c){if(page==PAGE_LIST&&view_count){add_to_playlist(view_indices[selected]);return;}if(page==PAGE_PLAYLISTS){start_new_playlist();return;}}
    if(key==SDLK_RETURN||key==SDLK_z||key==SDLK_SPACE){if(page==PAGE_HOME)activate_selected();else if(page==PAGE_LIST)activate_selected();else if(page==PAGE_PLAYLISTS)activate_selected();return;}
}
static void capture_binding(int kind,int code) {
    if(page!=PAGE_BUTTON_SETUP||!setup_active||setup_step>=8)return;
    button_map[setup_step].kind=kind;button_map[setup_step].code=code;
    setup_step++;
    if(setup_step>=8){setup_active=0;button_setup_done=1;save_config();page=PAGE_HOME;selected=0;toast_msg("BUTTON MAPPING SAVED");}
}
static int binding_matches(int index,int kind,int code) {
    return index>=0&&index<8&&button_map[index].kind==kind&&button_map[index].code==code;
}
static void dispatch_mapped(int index) {
    switch(index){
        case 0:handle_key(SDLK_UP);break;case 1:handle_key(SDLK_DOWN);break;
        case 2:handle_key(SDLK_LEFT);break;case 3:handle_key(SDLK_RIGHT);break;
        case 4:handle_key(SDLK_RETURN);break;case 5:handle_key(SDLK_ESCAPE);break;
        case 6:if(page==PAGE_NOW)play_next(1);else handle_key(SDLK_TAB);break;
        case 7:if(page==PAGE_NOW)play_next(-1);else handle_key(SDLK_TAB);break;
    }
}
static void handle_controller(const SDL_Event *e) {
    if(e->type==SDL_JOYBUTTONDOWN){
        int b=e->jbutton.button;
        if(page==PAGE_BUTTON_SETUP){capture_binding(1,b);return;}
        for(int i=0;i<8;i++)if(binding_matches(i,1,b)){dispatch_mapped(i);return;}
    } else if(e->type==SDL_JOYHATMOTION){
        int v=e->jhat.value;
        if(page==PAGE_BUTTON_SETUP){if(v==SDL_HAT_UP||v==SDL_HAT_DOWN||v==SDL_HAT_LEFT||v==SDL_HAT_RIGHT)capture_binding(2,v);return;}
        if(v==SDL_HAT_UP||v==SDL_HAT_DOWN||v==SDL_HAT_LEFT||v==SDL_HAT_RIGHT)
            for(int i=0;i<4;i++)if(binding_matches(i,2,v)){dispatch_mapped(i);return;}
    }
}
static void poll_player(void) {
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
    if(joystick)SDL_JoystickClose(joystick);
    if(renderer)SDL_DestroyRenderer(renderer);
    if(window)SDL_DestroyWindow(window);
    SDL_Quit();
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"--version")){puts("Aurora R36 Native C 0.3.0");return 0;}
    load_config();
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER)<0){fprintf(stderr,"SDL init failed: %s\n",SDL_GetError());return 1;}
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS,"0");
    Uint32 flags=SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_SHOWN; if(getenv("AURORA_WINDOW"))flags=SDL_WINDOW_SHOWN;
    window=SDL_CreateWindow(APP_NAME,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,W,H,flags);if(!window){fprintf(stderr,"SDL window failed: %s\n",SDL_GetError());SDL_Quit();return 1;}
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);if(!renderer){fprintf(stderr,"SDL renderer failed: %s\n",SDL_GetError());SDL_DestroyWindow(window);SDL_Quit();return 1;}
    if(pthread_create(&thumb_worker,NULL,thumb_worker_main,NULL)==0)thumb_worker_running=1;
    SDL_RenderSetLogicalSize(renderer,W,H);if(SDL_NumJoysticks()>0){joystick=SDL_JoystickOpen(0);}
    if(!mpv_start())toast_msg("MPV NOT FOUND - INSTALL MPV");else mpv_set_volume(volume);
    reload_library();selected=0;page=button_setup_done?PAGE_HOME:PAGE_BUTTON_SETUP;setup_step=0;setup_active=!button_setup_done;Uint32 last_tick=SDL_GetTicks();
    while(running){SDL_Event e;while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)running=0;else if(e.type==SDL_KEYDOWN)handle_key(e.key.keysym.sym);else if(e.type==SDL_TEXTINPUT&&text_entry){size_t n=strlen(playlist_name);if(n+strlen(e.text.text)<sizeof(playlist_name)-1)strcat(playlist_name,e.text.text);}else if(e.type==SDL_JOYBUTTONDOWN||e.type==SDL_JOYHATMOTION)handle_controller(&e);}
        if(page==PAGE_PLAYLISTS){/* list view is refreshed in renderer */}
        poll_player();if(SDL_GetTicks()-last_tick>16){render();last_tick=SDL_GetTicks();}SDL_Delay(2);
    }
    cleanup();return 0;
}
