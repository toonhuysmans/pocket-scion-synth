#include "pico2_menu.h"

#if PICO_RP2350

#include <stdio.h>
#include <string.h>
#include "display.h"
#include "pico/time.h"
#include "sam_voice.h"

enum { ROOT_GLOBAL, ROOT_PROGRAM, ROOT_PATCH, ROOT_SEQUENCE, ROOT_ARTICULATION,
       ROOT_MOTION, ROOT_SPEECH, ROOT_BANK, ROOT_SENSOR, ROOT_ACTIONS, ROOT_COUNT };
enum { LEVEL_ROOT, LEVEL_LANE, LEVEL_SECTION, LEVEL_LEAF };

typedef struct {
    uint8_t level, root, lane, section;
    uint16_t leaf;
    int8_t action_result;
    uint8_t saver_state, clear_band;
    uint64_t last_activity_us, next_display_us, input_guard_until_us;
    bool speech_showing, voice_was_active;
    uint8_t word_offset;
    uint64_t next_word_us;
    char speech_text[SAM_VOICE_PHRASE_LENGTH];
} menu_state_t;

static menu_state_t menu;
static const char *const root_names[ROOT_COUNT] = {
    "GLOBALS", "BANK/INSTRUMENT", "PATCH", "SEQUENCE", "ARTICULATION",
    "MOTION", "SPEECH", "BANK", "SENSOR", "SAVE/REVERT"
};
static const char *const root_abbrev[ROOT_COUNT] = {"GL","BI","P","SQ","AR","MO","SP","BK","SN","SV"};
static const char *const lane_names[3] = {"BASS", "PAD", "LEAD"};
static const char *const scene_sections[8] = {
    "OSCILLATORS", "FILTER", "MOD ENVELOPE", "VOICE",
    "LFO", "AMPLIFIER", "EXPRESSION", "EFFECTS"
};
static const uint8_t scene_section_count[8] = {8,5,6,4,7,7,4,6};
static const uint8_t scene_section_ids[47] = {
    0,1,2,3,4,5,6,7, 8,9,10,11,32, 12,13,14,15,16,17,
    18,19,35,40, 20,21,22,23,24,25,26, 27,28,29,30,31,33,34,
    36,37,38,39, 41,42,43,44,45,46
};
static const char *const scene_names[47] = {
    "OSC1 WAVE","OSC1 SHAPE","OSC1 MORPH","SUB/NOISE","OSC2 WAVE","OSC2 COARSE","OSC2 FINE","OSC MIX",
    "CUTOFF","RESONANCE","FILTER EG","KEY TRACK","MOD ATTACK","MOD DECAY","MOD SUSTAIN","MOD RELEASE","MOD OSC AMT","MOD OSC DEST",
    "VOICE MODE","PORTAMENTO","LFO WAVE","LFO RATE","LFO DEPTH","LFO FADE","LFO OSC AMT","LFO OSC DEST","LFO FILTER",
    "AMP GAIN","AMP ATTACK","AMP DECAY","AMP SUSTAIN","AMP RELEASE","FILTER MODE","AMP EG MOD","REL=DECAY","BEND RANGE",
    "BREATH FILTER","BREATH AMP","ENV VELOCITY","AMP VELOCITY","ASSIGN MODE","CHORUS MIX","CHORUS RATE","CHORUS DEPTH","DELAY FEEDBACK","DELAY TIME","DELAY MODE"
};
static const char *const global_names[18] = {
    "ROOT MIDI NOTE","SENSITIVITY","MASTER VOLUME","NOTE DURATION","SENSOR PITCH BEND","MIDI CHANNELS","LED BRIGHTNESS","RESPONSE WINDOW","NOISE REJECTION","ADAPTIVE NORMALIZE","PRESSURE SMOOTH","EXPRESSION SMOOTH","VARIATION GAIN","TRANSIENT GAIN","TRANSIENT DECAY","ACTIVITY TIMEOUT","CAL LEARNING","CAL RECOVERY"
};
static const uint16_t global_min[18] = {24,0,0,0,0,0,0,4,500,0,1,1,1,0,1,100,0,1};
static const uint16_t global_max[18] = {72,7,11,7,1,1,127,24,10000,100,100,100,200,200,100,10000,1,50};
static const char *const bank_names[19] = {
    "TEMPO MULTIPLIER","BREATH MAX","MODULATION MAX","CUTOFF RANGE","RESONANCE RANGE","MORPH RANGE","LFO RATE RANGE","BEND RESPONSE","DENSITY OFFSET","RATCHET RESPONSE","GATE MULTIPLIER","BASS MOTION","PAD MOTION","LEAD MOTION","LED RED","LED GREEN","LED BLUE","DEFAULT LOW MODE","PERC BALANCE"
};
static const uint16_t bank_min[19] = {40,0,0,0,0,0,0,0,8,0,25,0,0,0,0,0,0,1,0};
static const uint16_t bank_max[19] = {200,127,127,127,127,127,127,200,24,200,200,200,200,200,127,127,127,3,127};
static const char *const speech_names[10] = {"ENABLED","VOICE LEVEL","SPEED","PITCH","MOUTH","THROAT","PHRASE DENSITY","SENSOR INFLUENCE","MOTION CHANCE","MOTION AMOUNT"};
static const uint16_t speech_min[10] = {0,0,1,0,0,0,0,0,0,0};
static const uint16_t speech_max[10] = {1,127,255,255,255,255,127,127,127,127};
static const char *const sensor_names[4] = {"PROXIMITY","EXPRESSION","TRANSIENT","BEND"};
static const char *const action_names[6] = {"SAVE PATCH","SAVE BANK","SAVE GLOBALS","REVERT PATCH","REVERT BANK","REVERT GLOBALS"};
static const char *const program_names[2] = {"BANK", "INSTRUMENT"};
static const char *const global_sections[2]={"PERFORMANCE","SENSOR CALIBRATION"};
static const uint8_t global_counts[2]={7,11};
static const char *const sequence_sections[5]={"SCALE","MOTIF","CLOCK","RHYTHM","REGISTER"};
static const uint8_t sequence_counts[5]={7,16,2,12,2};
static const uint8_t sequence_ids[39]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,27,24,25,26,28,29,30,31,32,33,34,99,100,107,108};
static const char *const articulation_sections[7]={"LOW ROLE","ARTICULATION 1","ARTICULATION 2","ARTICULATION 3","ARTICULATION 4","ARTICULATION 5","ARTICULATION 6"};
static const uint8_t articulation_counts[7]={4,10,10,10,10,10,10};
static const char *const motion_sections[2]={"SENSOR ROUTING","ENVELOPE MOTION"};
static const uint8_t motion_counts[2]={7,3};
static const uint8_t motion_ids[10]={101,102,103,109,110,111,112,104,105,106};
static const char *const speech_sections[3]={"VOICE TRIGGER","SAM VOICE","VOICE MOTION"};
static const uint8_t speech_counts[3]={4,4,2};
static const uint8_t speech_ids[10]={0,1,6,7,2,3,4,5,8,9};
static const char *const bank_sections[6]={"CLOCK","SENSOR ROUTING","RHYTHM","LANE RESPONSE","DISPLAY COLOUR","LOW ROLE"};
static const uint8_t bank_counts[6]={1,7,3,3,3,2};

static uint16_t root_leaf_count(uint8_t root) {
    static const uint16_t counts[ROOT_COUNT] = {18,2,0,39,64,10,10,19,4,6};
    return counts[root];
}
static uint8_t scene_parameter(void) {
    unsigned base = 0;
    for (unsigned i = 0; i < menu.section; ++i) base += scene_section_count[i];
    return scene_section_ids[base + menu.leaf];
}
static unsigned section_total(uint8_t root,uint8_t lane){
    if(root==ROOT_PATCH)return lane==1u?8u:7u;
    if(root==ROOT_GLOBAL)return 2u;
    if(root==ROOT_SEQUENCE)return 5u;
    if(root==ROOT_ARTICULATION)return 7u;
    if(root==ROOT_MOTION)return 2u;
    if(root==ROOT_SPEECH)return 3u;
    if(root==ROOT_BANK)return 6u;
    return 0u;
}
static unsigned section_offset(const uint8_t *counts,unsigned section){unsigned o=0;for(unsigned i=0;i<section;++i)o+=counts[i];return o;}
static unsigned section_leaf_total(void){
    if(menu.root==ROOT_PATCH)return scene_section_count[menu.section];
    if(menu.root==ROOT_GLOBAL)return global_counts[menu.section];
    if(menu.root==ROOT_SEQUENCE)return sequence_counts[menu.section];
    if(menu.root==ROOT_ARTICULATION)return articulation_counts[menu.section];
    if(menu.root==ROOT_MOTION)return motion_counts[menu.section];
    if(menu.root==ROOT_SPEECH)return speech_counts[menu.section];
    if(menu.root==ROOT_BANK)return bank_counts[menu.section];
    return root_leaf_count(menu.root);
}
static const char *section_title(void){
    if(menu.root==ROOT_PATCH)return scene_sections[menu.section];
    if(menu.root==ROOT_GLOBAL)return global_sections[menu.section];
    if(menu.root==ROOT_SEQUENCE)return sequence_sections[menu.section];
    if(menu.root==ROOT_ARTICULATION)return articulation_sections[menu.section];
    if(menu.root==ROOT_MOTION)return motion_sections[menu.section];
    if(menu.root==ROOT_SPEECH)return speech_sections[menu.section];
    if(menu.root==ROOT_BANK)return bank_sections[menu.section];
    return "";
}
static uint8_t mapped_parameter(void){
    unsigned o;
    if(menu.root==ROOT_GLOBAL){o=section_offset(global_counts,menu.section);return (uint8_t)(o+menu.leaf);}
    if(menu.root==ROOT_SEQUENCE){o=section_offset(sequence_counts,menu.section);return sequence_ids[o+menu.leaf];}
    if(menu.root==ROOT_ARTICULATION){if(menu.section==0u)return (uint8_t)(35u+menu.leaf);return (uint8_t)(39u+(menu.section-1u)*10u+menu.leaf);}
    if(menu.root==ROOT_MOTION){o=section_offset(motion_counts,menu.section);return motion_ids[o+menu.leaf];}
    if(menu.root==ROOT_SPEECH){o=section_offset(speech_counts,menu.section);return speech_ids[o+menu.leaf];}
    if(menu.root==ROOT_BANK){o=section_offset(bank_counts,menu.section);return (uint8_t)(o+menu.leaf);}
    return (uint8_t)menu.leaf;
}
static void shared_name(uint8_t p, char *out, size_t n) {
    if (p < 7u) snprintf(out,n,"SCALE DEGREE %u",p+1u);
    else if (p < 23u) snprintf(out,n,"MOTIF STEP %u",p-6u);
    else if (p == 23u) snprintf(out,n,"TEMPO");
    else if (p < 27u) snprintf(out,n,"%s STEPS",lane_names[p-24u]);
    else if (p == 27u) snprintf(out,n,"SWING");
    else if (p < 31u) snprintf(out,n,"%s GATE",lane_names[p-28u]);
    else if (p == 31u) snprintf(out,n,"BASS DENSITY");
    else if (p < 35u) snprintf(out,n,"%s RATCHETS",lane_names[p-32u]);
    else if (p == 35u) snprintf(out,n,"LOW-LANE MODE");
    else if (p == 36u) snprintf(out,n,"PERC BALANCE");
    else if (p == 37u) snprintf(out,n,"SENSOR INFLUENCE");
    else if (p == 38u) snprintf(out,n,"VARIATION");
    else if (p == 99u) snprintf(out,n,"PAD DENSITY");
    else if (p == 100u) snprintf(out,n,"LEAD DENSITY");
    else if (p == 101u) snprintf(out,n,"BREATH MAX OVERRIDE");
    else if (p == 102u) snprintf(out,n,"PITCH-BEND RESPONSE");
    else if (p == 103u) snprintf(out,n,"RATCHET RESPONSE");
    else if (p < 107u) snprintf(out,n,"AMP %s MOTION",p==104u?"DECAY":p==105u?"SUSTAIN":"RELEASE");
    else if (p == 107u) snprintf(out,n,"PRESSURE OCTAVES");
    else if (p == 108u) snprintf(out,n,"EXPRESSION THRESH");
    else snprintf(out,n,"%s MOTION",p==109u?"CUTOFF":p==110u?"RESONANCE":p==111u?"MORPH":"LFO RATE");
}
static void shared_range(uint8_t p, uint16_t *lo, uint16_t *hi) {
    *lo=0; *hi=127;
    if (p<7u) *hi=48; else if(p<23u) *hi=6; else if(p==23u){*lo=40;*hi=240;}
    else if(p<27u){*lo=1;*hi=16;} else if(p==27u){*lo=50;*hi=75;}
    else if(p<31u){*lo=8;*hi=128;} else if(p==31u||p==99u||p==100u){*lo=8;*hi=24;}
    else if(p<35u||p==102u||p==103u||p>=109u)*hi=200;
    else if(p==35u)*hi=3; else if(p==107u)*hi=2; else if(p==108u)*hi=63;
}
static void articulation_name(uint16_t leaf, char *out, size_t n) {
    static const char *const fields[10]={"SOUND","RHYTHMIC ROLE","WEIGHT","LEVEL","TUNE","TONE","BODY/NOISE","DECAY","TRANSIENT","RATCHET RESPONSE"};
    snprintf(out,n,"ART%u %s",(unsigned)(leaf/10u+1u),fields[leaf%10u]);
}
static void articulation_range(uint16_t leaf,uint16_t *lo,uint16_t *hi){*lo=0;static const uint16_t m[10]={7,4,127,127,48,127,127,127,127,127};*hi=m[leaf%10u];}

static void current_meta(synth_t *s,uint8_t *scope,uint8_t *lane,uint8_t *param,uint16_t *target,const char **name,char *scratch,size_t cap,uint16_t *lo,uint16_t *hi,bool *editable){
    *editable=true; *target=0; *lane=0; *lo=0; *hi=127; *name="";
    if(menu.root==ROOT_GLOBAL){*scope=SYNTH_EDITOR_SCOPE_GLOBAL;*param=mapped_parameter();*name=global_names[*param];*lo=global_min[*param];*hi=global_max[*param];}
    else if(menu.root==ROOT_PROGRAM){*scope=0;*param=(uint8_t)menu.leaf;*name=program_names[*param];*lo=1;*hi=16;}
    else if(menu.root==ROOT_PATCH){*scope=SYNTH_EDITOR_SCOPE_PATCH;*target=synth_program_id(s);*lane=menu.lane;*param=scene_parameter();*name=scene_names[*param];if(*param==18u){*lo=2;*hi=5;}}
    else if(menu.root==ROOT_SEQUENCE){*scope=SYNTH_EDITOR_SCOPE_PATCH;*target=synth_program_id(s);*lane=3;*param=mapped_parameter();shared_name(*param,scratch,cap);*name=scratch;shared_range(*param,lo,hi);}
    else if(menu.root==ROOT_ARTICULATION){*scope=SYNTH_EDITOR_SCOPE_PATCH;*target=synth_program_id(s);*lane=3;*param=mapped_parameter();if(menu.section==0u){shared_name(*param,scratch,cap);shared_range(*param,lo,hi);}else{articulation_name((uint16_t)((menu.section-1u)*10u+menu.leaf),scratch,cap);articulation_range(menu.leaf,lo,hi);}*name=scratch;}
    else if(menu.root==ROOT_MOTION){*scope=SYNTH_EDITOR_SCOPE_PATCH;*target=synth_program_id(s);*lane=3;*param=mapped_parameter();shared_name(*param,scratch,cap);*name=scratch;shared_range(*param,lo,hi);}
    else if(menu.root==ROOT_SPEECH){*scope=SYNTH_EDITOR_SCOPE_PATCH;*target=synth_program_id(s);*lane=SYNTH_EDITOR_SPEECH_LANE;*param=mapped_parameter();*name=speech_names[*param];*lo=speech_min[*param];*hi=speech_max[*param];}
    else if(menu.root==ROOT_BANK){*scope=SYNTH_EDITOR_SCOPE_BANK;*target=s->bank_index;*param=mapped_parameter();*name=bank_names[*param];*lo=bank_min[*param];*hi=bank_max[*param];}
    else if(menu.root==ROOT_SENSOR){*scope=SYNTH_EDITOR_SCOPE_SENSOR;*param=(uint8_t)menu.leaf;*name=sensor_names[*param];*hi=1000;*editable=false;}
    else {*scope=0;*param=(uint8_t)menu.leaf;*name=action_names[*param];*hi=1;}
}

static void breadcrumb(char *out,size_t n){
    if(menu.root==ROOT_PATCH&&menu.level>=LEVEL_LANE){
        if(menu.level>=LEVEL_SECTION)snprintf(out,n,"P>%c>%u",lane_names[menu.lane][0],menu.section+1u);
        else snprintf(out,n,"P>%c",lane_names[menu.lane][0]);
    }else if(menu.level>=LEVEL_SECTION)snprintf(out,n,"%s>%u",root_abbrev[menu.root],menu.section+1u);
    else snprintf(out,n,"%s",root_abbrev[menu.root]);
}
static bool next_word(uint8_t offset,char *word,size_t capacity,uint8_t *next_offset){
    unsigned i=offset;
    while(menu.speech_text[i]!='\0'&&!((menu.speech_text[i]>='A'&&menu.speech_text[i]<='Z')||(menu.speech_text[i]>='a'&&menu.speech_text[i]<='z')||(menu.speech_text[i]>='0'&&menu.speech_text[i]<='9')))++i;
    if(menu.speech_text[i]=='\0')return false;
    unsigned length=0;
    while(menu.speech_text[i]!='\0'&&((menu.speech_text[i]>='A'&&menu.speech_text[i]<='Z')||(menu.speech_text[i]>='a'&&menu.speech_text[i]<='z')||(menu.speech_text[i]>='0'&&menu.speech_text[i]<='9')||menu.speech_text[i]=='-')){
        if(length+1u<capacity)word[length++]=menu.speech_text[i];
        ++i;
    }
    word[length]='\0';*next_offset=(uint8_t)i;return length!=0u;
}
void pico2_menu_init(void){menu=(menu_state_t){0};menu.last_activity_us=time_us_64();}
void pico2_menu_show(synth_t *s){
    char path[10];breadcrumb(path,sizeof path);display_set_breadcrumb(path);
    if(menu.level==LEVEL_ROOT){display_show_menu_node(root_names[menu.root],menu.root+1,ROOT_COUNT,s->program_index,s->bank_index,true);return;}
    if(menu.root==ROOT_PATCH&&menu.level==LEVEL_LANE){display_show_menu_node(lane_names[menu.lane],menu.lane+1,3,s->program_index,s->bank_index,true);return;}
    if(menu.level==LEVEL_SECTION){unsigned total=section_total(menu.root,menu.lane);display_show_menu_node(section_title(),menu.section+1,total,s->program_index,s->bank_index,true);return;}
    uint8_t scope,lane,param;uint16_t target,lo,hi,value=0;bool editable;const char *name;char scratch[28];
    current_meta(s,&scope,&lane,&param,&target,&name,scratch,sizeof scratch,&lo,&hi,&editable);
    if(menu.root==ROOT_ACTIONS)value=menu.action_result<0?0u:(uint16_t)menu.action_result;else if(menu.root==ROOT_PROGRAM)value=menu.leaf==0u?s->bank_index+1u:s->program_index+1u;else (void)synth_editor_get(s,scope,target,lane,param,&value);
    display_show_parameter(name,value,lo,hi,s->program_index,s->bank_index,true);
}
static void move_leaf(int d){uint16_t count=(uint16_t)section_leaf_total();menu.leaf=(uint16_t)((menu.leaf+count+(d<0?-1:1))%count);}
static void edit(synth_t *s,int d){
    if(menu.root==ROOT_PROGRAM){if(menu.leaf==0u)synth_set_bank_step(s,d);else synth_set_program_step(s,d);return;}
    if(menu.root==ROOT_ACTIONS){
        if(menu.action_result!=0)return;
        if(d>0){
            uint8_t scope=(uint8_t)(menu.leaf%3u);
            uint16_t target=scope==SYNTH_EDITOR_SCOPE_PATCH?synth_program_id(s):scope==SYNTH_EDITOR_SCOPE_BANK?s->bank_index:0;
            bool ok=menu.leaf<3u?synth_editor_commit(s,scope,target):synth_editor_revert(s,scope,target);
            menu.action_result=ok?1:-1;
        }
        return;
    }
    uint8_t scope,lane,param;uint16_t target,lo,hi,current;bool editable;const char *name;char scratch[28];
    current_meta(s,&scope,&lane,&param,&target,&name,scratch,sizeof scratch,&lo,&hi,&editable);if(!editable||!synth_editor_get(s,scope,target,lane,param,&current))return;
    uint16_t next=current;if(d<0&&current>lo)next=(uint16_t)(current-1u);else if(d>0&&current<hi)next=(uint16_t)(current+1u);else return;
    if(scope==SYNTH_EDITOR_SCOPE_PATCH&&lane<3u&&param==18u){if(d>0)next=current<4u?4u:5u;else next=current>4u?4u:2u;}
    (void)synth_editor_set(s,scope,target,lane,param,next);
}
bool pico2_menu_handle(synth_t *s,control_event_t e){
    if(e!=CONTROL_NONE){
        uint64_t event_time=time_us_64();menu.last_activity_us=event_time;
        if(menu.speech_showing){menu.speech_showing=false;pico2_menu_show(s);}
        if(menu.saver_state==1u||menu.saver_state==2u){menu.saver_state=3u;menu.clear_band=0u;menu.next_display_us=event_time;menu.input_guard_until_us=event_time+250000u;return true;}
        if(menu.saver_state==3u)return true;
        if(event_time<menu.input_guard_until_us){menu.input_guard_until_us=event_time+250000u;return true;}
    }
    if(e==CONTROL_PARAMETER_ENTER){if(menu.level==LEVEL_ROOT){if(menu.root==ROOT_PATCH)menu.level=LEVEL_LANE;else if(section_total(menu.root,menu.lane)>0u){menu.level=LEVEL_SECTION;menu.section=0;}else menu.level=LEVEL_LEAF;menu.leaf=0;}else if(menu.root==ROOT_PATCH&&menu.level==LEVEL_LANE){menu.level=LEVEL_SECTION;menu.section=0;}else if(menu.level==LEVEL_SECTION){menu.level=LEVEL_LEAF;menu.leaf=0;}pico2_menu_show(s);return true;}
    if(e==CONTROL_PARAMETER_BACK){if(menu.level==LEVEL_LEAF)menu.level=section_total(menu.root,menu.lane)>0u?LEVEL_SECTION:LEVEL_ROOT;else if(menu.level==LEVEL_SECTION)menu.level=menu.root==ROOT_PATCH?LEVEL_LANE:LEVEL_ROOT;else if(menu.level==LEVEL_LANE)menu.level=LEVEL_ROOT;pico2_menu_show(s);return true;}
    if(e==CONTROL_PARAMETER_NEXT||e==CONTROL_PARAMETER_PREVIOUS){int d=e==CONTROL_PARAMETER_NEXT?1:-1;menu.action_result=0;if(menu.level==LEVEL_ROOT)menu.root=(uint8_t)(d<0?(menu.root+ROOT_COUNT-1u)%ROOT_COUNT:(menu.root+1u)%ROOT_COUNT);else if(menu.root==ROOT_PATCH&&menu.level==LEVEL_LANE)menu.lane=(uint8_t)(d<0?(menu.lane+2u)%3u:(menu.lane+1u)%3u);else if(menu.level==LEVEL_SECTION){unsigned total=section_total(menu.root,menu.lane);menu.section=(uint8_t)(d<0?(menu.section+total-1u)%total:(menu.section+1u)%total);}else move_leaf(d);pico2_menu_show(s);return true;}
    if(e==CONTROL_PARAMETER_INCREASE||e==CONTROL_PARAMETER_DECREASE){if(menu.level==LEVEL_LEAF)edit(s,e==CONTROL_PARAMETER_INCREASE?1:-1);pico2_menu_show(s);return true;}
    return false;
}
void pico2_menu_service(synth_t *s){
    uint64_t now=time_us_64();
    bool voice_active=sam_voice_active();
    if(voice_active&&!menu.voice_was_active&&s->speech_last_phrase<SAM_VOICE_PHRASE_COUNT){
        if(synth_editor_get_phrase(s,synth_program_id(s),s->speech_last_phrase,
                                   menu.speech_text,sizeof(menu.speech_text))){
            menu.speech_showing=true;menu.word_offset=0u;menu.next_word_us=now;
            menu.last_activity_us=now;
        }
    }
    menu.voice_was_active=voice_active;
    if(menu.speech_showing){
        if(now>=menu.next_word_us){
            char word[20];uint8_t next_offset;
            if(next_word(menu.word_offset,word,sizeof(word),&next_offset)){
                if(display_show_word(word)){
                    menu.word_offset=next_offset;uint16_t speed=72u;
                    (void)synth_editor_get(s,SYNTH_EDITOR_SCOPE_PATCH,synth_program_id(s),
                                           SYNTH_EDITOR_SPEECH_LANE,2u,&speed);
                    unsigned duration=120u+(unsigned)strlen(word)*55u;
                    duration=duration*(speed==0u?1u:speed)/72u;
                    if(duration<180u)duration=180u;
                    if(duration>1400u)duration=1400u;
                    menu.next_word_us=now+(uint64_t)duration*1000u;
                }
            }else if(voice_active){menu.next_word_us=now+100000u;}
            else{menu.speech_showing=false;menu.last_activity_us=now;
                 if(menu.saver_state==2u)menu.next_display_us=now;else pico2_menu_show(s);}
        }
        return;
    }
    if(menu.saver_state==0u&&now-menu.last_activity_us>=15000000u){menu.saver_state=1u;menu.clear_band=0u;menu.next_display_us=now;}
    if((menu.saver_state==1u||menu.saver_state==3u)&&now>=menu.next_display_us){
        display_clear_band(menu.clear_band++);menu.next_display_us=now+10000u;
        if(menu.clear_band>=14u){if(menu.saver_state==1u)menu.saver_state=2u;else{menu.saver_state=0u;pico2_menu_show(s);}}
        return;
    }
    if(menu.saver_state==2u&&now>=menu.next_display_us){
        uint8_t phase=(uint8_t)((s->transport_frame>>11u)&63u);
        uint8_t motion=(uint8_t)(s->sequence_step+s->visual_lfo+(uint8_t)s->note_on_counter);
        uint8_t density=(uint8_t)(s->euclid_pulses[0]+s->euclid_pulses[1]+s->euclid_pulses[2]);
        uint8_t sensor=(uint8_t)(s->sensor_expression*127.0f);
        display_screensaver_step(phase,motion,density,sensor);menu.next_display_us=now+50000u;
    }
}
#endif
