#include "MSPd.h"
// #include "fftease.h"
#define OSCBANK_TABLE_LENGTH (8192)
#define OSCBANK_DEFAULT_TOPFREQ (15000.0)

#define MAXSTORE (128)

#define OBJECT_NAME "splitbank~"

static t_class *splitbank_class;

typedef struct
{
  int R;
  int N;
  int N2;
  int Nw;
  int Nw2;
  int vector_size;
  int i;
  int in_count;
  float *Wanal;
  float *Wsyn;
  float *input_buffer;
  float *Hwin;
  float *complex_spectrum;
  float *interleaved_spectrum;
  float *output_buffer;
  // for convert
  float *c_lastphase_in;
  float *c_lastphase_out;
  float c_fundamental;
  float c_factor_in;
  float c_factor_out;
  float P;
  int table_length;
  float table_si;
  int first;
  float i_vector_size;
  float *lastamp;
  float *lastfreq;
  float *index;
  float *table;
  float pitch_increment;

  int lo_bin;
  int hi_bin;
  float synthesis_threshold;

  int overlap;
  int winfac;
  float user_lofreq;
  float user_hifreq;
  float curfreq;
  // faster FFT
  float mult;
  float *trigland;
  int *bitshuffle;

} t_oscbank;

typedef struct _splitbank
{
  t_object x_obj;
  t_float x_f;
  t_oscbank **obanks;
  int N;
  int N2;
  int R;
  int overlap;
  void *list_outlet;
  t_atom *list_data;
  int *bin_tmp;
  int ramp_frames;
  int frames_left;
  float frame_duration;
  int vector_size;
  int table_offset;
  int bin_offset;
  float *last_mag;
  float *current_mag;
  int *last_binsplit;
  int *current_binsplit;
  int **stored_binsplits;
  short *stored_slots;
  float *in_amps;
  short new_distribution;
  short interpolation_completed;
//    short bypass;
  short initialize;
  short manual_override;
  float manual_control_value;
  short mute;
  short powerfade;
  int channel_count;
  long countdown_samps; // samps for a given fadetime
  long counter;
  int hopsamps;
  t_float **ins; // input signal vectors
  t_float **outs; // output signal vectors
} t_splitbank;

static void *splitbank_new(t_symbol *s, int argc, t_atom *argv);
static t_int *splitbank_perform( t_int *w );
static void splitbank_dsp(t_splitbank *x, t_signal **sp);
static void splitbank_showstate( t_splitbank *x );
static void splitbank_manual_override( t_splitbank *x, t_floatarg toggle );
static void splitbank_setstate( t_splitbank *x, t_symbol *msg, int argc, t_atom *argv);
static void splitbank_ramptime( t_splitbank *x, t_symbol *msg, int argc, t_atom *argv);
static int rand_index( int max);
static void splitbank_scramble (t_splitbank *x);
static void splitbank_store( t_splitbank *x, t_floatarg location );
static void splitbank_recall( t_splitbank *x, t_floatarg location );
static void splitbank_powerfade( t_splitbank *x, t_floatarg toggle );
static void splitbank_maxfreq( t_splitbank *x, t_floatarg freq );
static void splitbank_minfreq( t_splitbank *x, t_floatarg freq );
static void splitbank_mute( t_splitbank *x, t_floatarg toggle );
static void splitbank_fftinfo( t_splitbank *x);
static void splitbank_free( t_splitbank *x );
static void splitbank_overlap( t_splitbank *x, t_floatarg ofac );
static void splitbank_spliti( t_splitbank *x,  float *dest_mag, int start, int end, float oldfrac);
static void splitbank_split(t_splitbank *x, int *binsplit, float *dest_mag, int start, int end );
static int splitbank_closestPowerOfTwo(int p);
static void fftease_obank_analyze( t_oscbank *x ) ;
static void fftease_obank_initialize ( t_oscbank *x, float lo_freq, float hi_freq, int overlap,
                                int R, int vector_size, int N);
// static void fftease_obank_transpose( t_oscbank *x );
static void fftease_obank_synthesize( t_oscbank *x );
static void fftease_obank_destroy( t_oscbank *x );
static void fftease_shiftin( t_oscbank *x, float *input );
static void fftease_shiftout( t_oscbank *x, float *output );
static void fftease_obank_topfreq( t_oscbank *x, float topfreq );
static void fftease_obank_bottomfreq( t_oscbank *x, float bottomfreq );


static void rfft( float *x, int N, int forward );
static void cfft( float *x, int NC, int forward );
static void bitreverse( float *x, int N );
static void fold( float *I, float *W, int Nw, float *O, int N, int n );
static void init_rdft(int n, int *ip, float *w);
static void rdft(int n, int isgn, float *a, int *ip, float *w);
static void bitrv2(int n, int *ip, float *a);
static void cftsub(int n, float *a, float *w);
static void rftsub(int n, float *a, int nc, float *c);
static void makewt(int nw, int *ip, float *w);
static void makect(int nc, int *ip, float *c);
static void makewindows( float *H, float *A, float *S, int Nw, int N, int I );
static void makehamming( float *H, float *A, float *S, int Nw, int N, int I,int odd );
static void makehanning( float *H, float *A, float *S, int Nw, int N, int I,int odd );
static void convert(float *S, float *C, int N2, float *lastphase, float fundamental, float factor );

//////////
void splitbank_tilde_setup(void) {

  splitbank_class = class_new(gensym("splitbank~"), (t_newmethod)splitbank_new,
                              (t_method)splitbank_free, sizeof(t_splitbank),0,A_GIMME,0);
  CLASS_MAINSIGNALIN(splitbank_class, t_splitbank, x_f);
  class_addmethod(splitbank_class, (t_method)splitbank_dsp, gensym("dsp"), A_CANT, 0);
  class_addmethod(splitbank_class, (t_method)splitbank_showstate, gensym("showstate"),0);
  class_addmethod(splitbank_class, (t_method)splitbank_manual_override, gensym("manual_override"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_store, gensym("store"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_mute, gensym("mute"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_recall, gensym("recall"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_setstate, gensym("setstate"),A_GIMME,0);
  class_addmethod(splitbank_class, (t_method)splitbank_ramptime, gensym("ramptime"),A_GIMME,0);
  class_addmethod(splitbank_class, (t_method)splitbank_powerfade, gensym("powerfade"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_maxfreq, gensym("maxfreq"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_minfreq, gensym("minfreq"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_overlap, gensym("overlap"),A_FLOAT,0);
  class_addmethod(splitbank_class, (t_method)splitbank_scramble, gensym("scramble"),0);
  class_addmethod(splitbank_class, (t_method)splitbank_fftinfo, gensym("fftinfo"),0);

  potpourri_announce(OBJECT_NAME);
}

void splitbank_overlap( t_splitbank *x, t_floatarg ofac )
{
  x->overlap = splitbank_closestPowerOfTwo( (int)ofac );
}

void splitbank_powerfade( t_splitbank *x, t_floatarg toggle )
{
  x->powerfade = (short)toggle;
}
void splitbank_mute( t_splitbank *x, t_floatarg toggle )
{
  x->mute = (short)toggle;
}
void splitbank_fftinfo( t_splitbank *x)
{
  post("FFT size: %d", x->N);
  post("Overlap Factor: %d", x->overlap);
  post("Hop size: %d", x->hopsamps);
}

void splitbank_manual_override( t_splitbank *x, t_floatarg toggle )
{
  x->manual_override = (short)toggle;
}

void splitbank_free( t_splitbank *x )
{
    /*

     */
    int i;
    if(x->initialize == 0) {
        freebytes(x->list_data, (x->N + 2) * sizeof(t_atom)) ;
        freebytes(x->current_binsplit, x->N2 * sizeof(int));
        freebytes(x->last_binsplit, x->N2 * sizeof(int));
        freebytes(x->current_mag, x->N2 * sizeof(float));
        freebytes(x->last_mag, x->N2 * sizeof(float));
        freebytes(x->bin_tmp, x->N2 * sizeof(int));
        freebytes(x->stored_slots, x->N2 * sizeof(short));
        freebytes(x->in_amps, (x->N +2) * sizeof(float));
        for( i = 0; i < MAXSTORE; i++ ) {
            freebytes(x->stored_binsplits[i], x->N2 * sizeof(int));
        }
        freebytes(x->stored_binsplits, MAXSTORE * sizeof(int *));
        
        for(i = 0; i < x->channel_count + 5; i++) {
            freebytes(x->ins[i], 8192 * sizeof(t_float));
        }
        freebytes(x->ins, sizeof(t_float *) * (x->channel_count + 5));
        freebytes(x->outs, sizeof(t_float *) * (x->channel_count + 1));
        for(i = 0; i < x->channel_count; i++) {
            fftease_obank_destroy(x->obanks[i]);
        }
        freebytes(x->obanks, x->channel_count * sizeof(t_oscbank *));
    }
}

void splitbank_maxfreq( t_splitbank *x, t_floatarg freq )
{
  int i;
  for(i = 0; i < x->channel_count; i++) {
    fftease_obank_topfreq( x->obanks[i], freq);
  }
}

void splitbank_minfreq( t_splitbank *x, t_floatarg freq )
{
  int i;
  for(i = 0; i < x->channel_count; i++) {
    fftease_obank_bottomfreq( x->obanks[i], freq);
  }
}

void splitbank_store( t_splitbank *x, t_floatarg loc )
{
  int **stored_binsplits = x->stored_binsplits;
  int *current_binsplit = x->current_binsplit;
  short *stored_slots = x->stored_slots;
  int location = (int) loc;
  int i;

  if( location < 0 || location > MAXSTORE - 1 ) {
    pd_error(0, "location must be between 0 and %d, but was %d", MAXSTORE, location);
    return;
  }
  for(i = 0; i < x->N2; i++ ) {
    stored_binsplits[location][i] = current_binsplit[i];
  }
  stored_slots[location] = 1;

  // post("stored bin split at location %d", location);
}

void splitbank_recall( t_splitbank *x, t_floatarg loc )
{
  int **stored_binsplits = x->stored_binsplits;
  int *current_binsplit = x->current_binsplit;
  int *last_binsplit = x->last_binsplit;
  short *stored_slots = x->stored_slots;
  int i;
  int location = (int) loc;
  if( location < 0 || location > MAXSTORE - 1 ) {
    pd_error(0, "location must be between 0 and %d, but was %d", MAXSTORE, location);
    return;
  }
  if( ! stored_slots[location] ) {
    pd_error(0, "nothing stored at location %d", location);
    return;
  }

  for(i = 0; i < x->N2; i++ ) {
    last_binsplit[i] = current_binsplit[i];
    current_binsplit[i] = stored_binsplits[location][i];
  }

  x->new_distribution = 1;
  x->interpolation_completed = 0;
  x->frames_left = x->ramp_frames;
  if(! x->ramp_frames) { // Ramp Off - Immediately set last to current
    for( i = 0; i < x->N2; i++ ) {
      x->last_binsplit[ i ] = x->current_binsplit[ i ];
    }
  }
}

int splitbank_closestPowerOfTwo(int p) {
  int base = 2;
  if(p > 2) {
    while(base < p) {
      base *= 2;
    }
  }
  return base;
}

void *splitbank_new(t_symbol *s, int argc, t_atom *argv)
{
  t_splitbank *x = (t_splitbank *)pd_new(splitbank_class);
  int i;


  x->channel_count = (int) atom_getfloatarg(0, argc, argv);
  x->channel_count = splitbank_closestPowerOfTwo( x->channel_count );
  // post("theoretic chan count: %d",x->channel_count );
  // x->channel_count = 8;
  srand( time( 0 ) );
  for(i = 0; i < x->channel_count + 4; i++) {
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("signal"),gensym("signal"));
  }
  for(i = 0; i < x->channel_count + 1; i++) {
    outlet_new(&x->x_obj, gensym("signal"));
  }

  x->ins = (t_float **) getbytes(sizeof(t_float *) * (x->channel_count + 5));
  x->outs = (t_float **) getbytes(sizeof(t_float *) * (x->channel_count + 1));
  for(i = 0; i < x->channel_count + 5; i++) {
    x->ins[i] = (t_float *) getbytes(8192 * sizeof(t_float));
  }
  x->list_outlet = (t_outlet *) outlet_new(&x->x_obj, gensym("list"));

  x->obanks = (t_oscbank **) getbytes(x->channel_count * sizeof(t_oscbank *));
  for(i = 0; i < x->channel_count; i++) {
    x->obanks[i] = (t_oscbank *) getbytes(sizeof(t_oscbank));
  }

  x->mute = 0;
  x->table_offset = 0;
  x->bin_offset = 0;
  x->powerfade = 0;
  x->manual_override = 0;
  x->countdown_samps = 0;
  x->overlap = 8; // to generate FFT size
  x->initialize = 1;
  return x;
}
t_int *splitbank_perform(t_int *w)
{

  int i,j;
  float frac = 0.0;
  t_splitbank *x = (t_splitbank *) (w[1]);

  int channel_count = x->channel_count;
  float *input;
  float *synthesis_threshold;
  float *t_offset;
  float *b_offset;
  float *manual_control;
  float *sync = (t_float *)(w[(channel_count * 2) + 7]);
  int n = (int) w[(channel_count * 2) + 8];

  int N2 = x->N2;
  int N = x->N;
  int hopsamps = x->hopsamps;
//    int frames_left = x->frames_left;
//    int ramp_frames = x->ramp_frames;

  int *current_binsplit = x->current_binsplit;
  int *last_binsplit = x->last_binsplit;

  float *in_amps = x->in_amps;
  float manual_control_value = x->manual_control_value;

  long counter = x->counter;
  long countdown_samps = x->countdown_samps;

  t_oscbank **obanks = x->obanks;


  t_float **ins = x->ins;
  t_float *inlet;
  t_float *outlet;
  t_float **outs = x->outs; // assign from output vector pointers

  // mute branch: clear outlets and return

  if(x->mute) {
    for(i = 0; i < (channel_count + 1); i++) {
      outlet = (t_float *) w[i + (channel_count + 7)];
      for(j = 0; j < n; j++) {
        outlet[j] = 0.0;
      }
    }
    return (w + ((channel_count * 2) + 9));
  }

  // Copy all inlets

  for(i = 0; i < channel_count + 5; i++) {
    inlet = (t_float *) w[2 + i];
    for(j = 0; j < n; j++) {
      ins[i][j] = inlet[j];
    }
  }
  // local assignments:

  input = ins[0];

  synthesis_threshold = ins[channel_count + 1];
  t_offset = ins[channel_count + 2];
  b_offset = ins[channel_count + 3];
  manual_control = ins[channel_count + 4];

  // assign outlet pointers

  for(i = 0; i < (channel_count + 1); i++) {
    outs[i] = (t_float *) w[i + (channel_count + 7)]; // was 5
  }

  sync = outs[channel_count];

  for(i = 0; i < channel_count; i++) {
    obanks[i]->pitch_increment = ins[i+1][0] * obanks[i]->table_si;
    obanks[i]->synthesis_threshold = synthesis_threshold[0];
  }


  x->table_offset = t_offset[0] * N2;
  x->bin_offset = b_offset[0] * N2;

  manual_control_value = manual_control[0];


  // ANALYSIS (only analyze to one oscbank

  fftease_shiftin( obanks[0], input );
  fftease_obank_analyze( obanks[0] );


  // copy input amplitudes from analyzed frame
  for( i = 0, j = 0; i < N; i += 2 , j++) {
    in_amps[j] = obanks[0]->interleaved_spectrum[i];
  }

  // zero the amps next

  for(i = 0; i < channel_count; i++) {
    for(j = 0; j < N; j += 2) {
      obanks[i]->interleaved_spectrum[j] = 0.0;
    }
  }

  if( x->manual_override ) {
    for(i = 0; i < channel_count; i++) {
      splitbank_spliti( x, obanks[i]->interleaved_spectrum,
                        N2*i/channel_count, N2*(i+1)/channel_count, manual_control_value);
    }
    frac = manual_control_value;
  }
  else if( x->new_distribution ) {

    x->new_distribution = 0;

    for(i = 0; i < channel_count; i++) {
      splitbank_split( x, last_binsplit, obanks[i]->interleaved_spectrum,
                       N2*i/channel_count, N2*(i+1)/channel_count);
    }
    frac = 0.0;
  }
  else if ( x->interpolation_completed ) {
    for(i = 0; i < channel_count; i++) {
      splitbank_split( x, current_binsplit, obanks[i]->interleaved_spectrum,
                       N2*i/channel_count, N2*(i+1)/channel_count);
    }
    frac = 1.0;
  } else {
    frac = (float) counter / (float) countdown_samps;

    for(i = 0; i < channel_count; i++) {
      splitbank_spliti( x, obanks[i]->interleaved_spectrum,
                        N2*i/channel_count, N2*(i+1)/channel_count, 1.0 - frac);
    }
    counter += hopsamps;
    if( counter >= countdown_samps )
    {
      counter = countdown_samps;
      x->interpolation_completed = 1;
    }
  }
  for( i = 0; i < n; i++ ) {
    sync[i] = frac;
  }
  // copy frequency information to other banks

  for(i = 1; i < channel_count; i++) {
    for( j = 1; j < N; j += 2) {
      obanks[i]->interleaved_spectrum[j] = obanks[0]->interleaved_spectrum[j];
    }
  }

  // SYNTHESIS

  for(i = 0; i < channel_count; i++) {
    fftease_obank_synthesize( obanks[i] );
    fftease_shiftout( obanks[i], outs[i] );
  }
  x->counter = counter;
  return (w + ((channel_count * 2) + 9));
}


void splitbank_scramble (t_splitbank *x)
{
  int i, j;
  int used;

  int max = x->N2;
  int bindex;

  int *current_binsplit = x->current_binsplit;
  int *last_binsplit = x->last_binsplit;
  int *bin_tmp = x->bin_tmp;

  x->new_distribution = 1;
  x->interpolation_completed = 0;

  //  post("scrambling");

  // Copy current mapping to last mapping (first time this will be all zeros)

  for( i = 0; i < x->N2; i++ ) {
    last_binsplit[i] = current_binsplit[i];
  }


  for( i = 0; i < max; i++ ) {
    bin_tmp[i] = i;
  }

  used = max;

  // This randomly distributes each bin number (to occur once each in a random location)

  for( i = 0; i < max; i++ ) {
    bindex = rand_index( used );
    current_binsplit[i] = bin_tmp[bindex];
    for(j = bindex; j < used - 1; j++) {
      bin_tmp[j] = bin_tmp[j+1];
    }
    --used;
  }
  x->counter = 0;
  if(! x->countdown_samps ) { // Ramp Off - Immediately set last to current
    for( i = 0; i < x->N2; i++ ) {
      last_binsplit[ i ] = current_binsplit[ i ];
    }
  }
}

int rand_index( int max) {
  int rand();
  return ( rand() % max );
}

void splitbank_setstate (t_splitbank *x, t_symbol *msg, int argc, t_atom *argv) {
  short i;

  if( argc != x->N2 ) {
    pd_error(0, "list must be of length %d, but actually was %d", x->N2, argc);
    return;
  }
  for( i = 0; i < x->N2; i++ ) {
    x->last_binsplit[ i ] = x->current_binsplit[ i ];
    x->current_binsplit[ i ] = 0;
  }
  for (i=0; i < argc; i++) {
    x->current_binsplit[i] = atom_getintarg(i, argc, argv );

  }
  x->frames_left = x->ramp_frames;
  if(! x->ramp_frames) { // Ramp Off - Immediately set last to current
    for( i = 0; i < x->N2; i++ ) {
      x->last_binsplit[ i ] = x->current_binsplit[ i ];
    }
  }

  return;
}

void splitbank_ramptime (t_splitbank *x, t_symbol *msg, int argc, t_atom *argv) {
  float rampdur;
  rampdur = atom_getfloatarg(0,argc,argv) * 0.001;
  x->countdown_samps = rampdur * x->R;
  x->counter = 0;

//    return;
}

// REPORT CURRENT SHUFFLE STATUS
void splitbank_showstate (t_splitbank *x ) {

  t_atom *list_data = x->list_data;

  short i, count;

  count = 0;
  // post("showing %d data points", x->N2);

  for( i = 0; i < x->N2; i++ ) {
    SETFLOAT(list_data+count,x->current_binsplit[i]);
    ++count;
  }
  outlet_list(x->list_outlet,0L,x->N2,list_data);

  return;
}
/*
  void splitbank_float(t_splitbank *x, t_float f) // Look at floats at inlets
  {
  int inlet = ((t_pxobject*)x)->z_in;
  int N2 = x->obank1->N2;

  // inlet 0 is the first signal inlet

  if (inlet == 1)
  {
  x->obank1->pitch_increment = f * x->obank1->table_si;
  }
  else if (inlet == 2)
  {
  x->obank2->pitch_increment = f * x->obank2->table_si;
  }
  else if (inlet == 3)
  {
  x->obank3->pitch_increment = f * x->obank3->table_si;
  }
  else if (inlet == 4)
  {
  x->obank4->pitch_increment = f * x->obank4->table_si;
  }
  else if (inlet == 5)
  {
  x->obank5->pitch_increment = f * x->obank5->table_si;
  }
  else if (inlet == 6)
  {
  x->obank6->pitch_increment = f * x->obank6->table_si;
  }
  else if (inlet == 7)
  {
  x->obank7->pitch_increment = f * x->obank7->table_si;
  }
  else if (inlet == 8)
  {
  x->obank8->pitch_increment = f * x->obank8->table_si;
  }
  else if (inlet == 9)
  {
  x->obank1->synthesis_threshold = f;
  x->obank2->synthesis_threshold = f;
  x->obank3->synthesis_threshold = f;
  x->obank4->synthesis_threshold = f;
  x->obank5->synthesis_threshold = f;
  x->obank6->synthesis_threshold = f;
  x->obank7->synthesis_threshold = f;
  x->obank8->synthesis_threshold = f;
  }
  else if (inlet == 10)
  {
  x->table_offset = (int) (f * N2);
  }
  else if (inlet == 11)
  {
  x->bin_offset = (int) (f * N2);
  }
  else if (inlet == 12)
  {
  x->manual_control_value = f;
  }
  }
*/
void splitbank_split(t_splitbank *x, int *binsplit, float *dest_mag, int start, int end )
{
  int i;
  int bindex;
  int n = x->N2;
  float *in_amps = x->in_amps;
  int table_offset = x->table_offset;
  int bin_offset = x->bin_offset;

  if( table_offset  < 0 )
    table_offset *= -1;
  if( bin_offset  < 0 )
    bin_offset *= -1;

  for( i = start; i < end; i++) {
    bindex = binsplit[ (i + table_offset) % n ];
    bindex = ( bindex + bin_offset ) % n;
    dest_mag[ bindex * 2 ] = in_amps[ bindex ]; // putting amps into interleaved spectrum
  }
}


void splitbank_spliti( t_splitbank *x, float *dest_mag, int start, int end, float oldfrac)
{
  int i;
  int bindex;
  int *current_binsplit = x->current_binsplit;
  int *last_binsplit = x->last_binsplit;
  float *current_mag = x->current_mag;
  float *last_mag = x->last_mag;
  float *in_amps = x->in_amps;
  int bin_offset = x->bin_offset;
  int table_offset = x->table_offset;
  int n = x->N2;
  float newfrac;
  float phase;


  if( oldfrac < 0 )
    oldfrac = 0;
  if( oldfrac > 1.0 )
    oldfrac = 1.0;

  if( x->powerfade ) {
    phase = oldfrac * PIOVERTWO;
    oldfrac = sin( phase );
    newfrac = cos( phase );
  } else {
    newfrac = 1.0 - oldfrac;
  }

  if( table_offset  < 0 )
    table_offset *= -1;
  if( bin_offset  < 0 )
    bin_offset *= -1;

  for( i = 0; i < n; i++ ) {
    last_mag[i] = current_mag[i] = 0.0;
  }

  for( i = start; i < end; i++ ) {
    bindex = current_binsplit[ (i + table_offset) % n ];
    bindex = ( bindex + bin_offset ) % n;
    current_mag[ bindex ] = in_amps[ bindex ];

    bindex = last_binsplit[ (i + table_offset) % n ];
    bindex = ( bindex + bin_offset ) % n;
    last_mag[ bindex ] = in_amps[ bindex ];
  }
  for( i = 0; i < n; i++) {
    if(! current_mag[i] && ! last_mag[i]) {
      dest_mag[i * 2] = 0.0;
    }
    else if( current_mag[i] && last_mag[i]) {
      dest_mag[i * 2] = current_mag[i];
    } else if (  current_mag[i] && ! last_mag[i] ) {
      dest_mag[i * 2] = newfrac * current_mag[i];
    }
    else {
      dest_mag[i * 2] = oldfrac * last_mag[i];
    }
  }
}

void splitbank_dsp(t_splitbank *x, t_signal **sp)
{
  int i;
  int R;
  int lo_freq = 0;
  int hi_freq = 15000;
  int fftsize;
  int overlap = x->overlap;
  t_int **sigvec;
  int pointer_count;
  int channel_count = x->channel_count;
  int vector_size;
  t_oscbank **obanks = x->obanks;

  pointer_count = (channel_count * 2) + 8;
  sigvec = (t_int **) getbytes(sizeof(t_int *) * pointer_count);
  for(i = 0; i < pointer_count; i++) {
    sigvec[i] = (t_int *) getbytes(sizeof(t_int) * 1);
  }
  sigvec[0] = (t_int *)x; // first pointer is to the object
  sigvec[pointer_count - 1] = (t_int *)sp[0]->s_n; // last pointer is to vector size (N)
  for(i = 1; i < pointer_count - 1; i++){ // now attach the inlet and all outlets
    sigvec[i] = (t_int *)sp[i-1]->s_vec;
  }

  x->vector_size = vector_size = sp[0]->s_n;

  fftsize = vector_size * overlap;

//    post("vector size %d, sys vector size: %d",vector_size, sys_getblksize() );
//    post("splitbank~: samples per vector: %d, sys blocksize %d, fftsize %d",
//         sp[0]->s_n, sys_getblksize(), fftsize);

  // generate FFT size from x->overlap * x->vector_size
  if( ! sp[0]->s_sr ) {
    pd_error(0, "splitbank~: zero sample rate! Perhaps no audio driver is selected.");
    return;
  }
  if(x->initialize || x->R != sys_getsr() || x->vector_size != sp[0]->s_n || x->N != fftsize) {
    if( (x->initialize || x->R != sys_getsr()) && (! x->countdown_samps) ) {
      x->counter = 0;
      x->countdown_samps = 1.0 * x->R; // 1 second fade time by default
    }
    x->R = sys_getsr();
    R = (int) x->R;
    x->N = fftsize;
    x->N2 = fftsize / 2;
    x->list_data = getbytes((x->N + 2) * sizeof(t_atom));
    x->last_binsplit = getbytes(x->N2 * sizeof(int));
    x->current_binsplit = getbytes(x->N2 * sizeof(int));
    x->bin_tmp = getbytes(x->N2 * sizeof(int));
    x->last_mag = getbytes(x->N2 * sizeof(float));
    x->current_mag = getbytes(x->N2 * sizeof(float));
    x->stored_slots = getbytes(x->N2 * sizeof(short));
    x->stored_binsplits = getbytes(MAXSTORE * sizeof(int *));
    for( i = 0; i < MAXSTORE; i++ ) {
      x->stored_binsplits[i] = getbytes(x->N2 * sizeof(int));
    }
    splitbank_scramble( x );

    for( i = 0; i < x->N2; i++ ) {
      x->last_binsplit[i] = x->current_binsplit[i];
    }
    for(i = 0; i < channel_count; i++) {
      fftease_obank_initialize(obanks[i], lo_freq, hi_freq, overlap, R, vector_size,x->N);
    }

    x->in_amps = getbytes((x->N +2) * sizeof(float));
    x->initialize = 0;
  }
  x->hopsamps = x->N / x->overlap;
  dsp_addv(splitbank_perform, pointer_count, (t_int *) sigvec);
  freebytes(sigvec, sizeof(t_int *) * pointer_count);
}

////////////////////////////////////////
/**************************************************/
void fftease_obank_destroy( t_oscbank *x )
{
  freebytes(x->Wanal, x->Nw * sizeof(float));
    freebytes(x->Wsyn, x->Nw * sizeof(float));
    freebytes(x->Hwin, x->Nw * sizeof(float));
    freebytes(x->complex_spectrum, x->N * sizeof(float));
    freebytes(x->interleaved_spectrum, (x->N + 2) * sizeof(float));
    freebytes(x->input_buffer, x->Nw * sizeof(float));
    freebytes(x->output_buffer, x->Nw * sizeof(float));
    freebytes(x->c_lastphase_in, (x->N2+1) * sizeof(float));
    freebytes(x->c_lastphase_out, (x->N2+1) * sizeof(float));
    freebytes(x->lastamp, (x->N+1) * sizeof(float));
    freebytes(x->lastfreq, (x->N+1) * sizeof(float));
    freebytes(x->index, (x->N+1) * sizeof(float));
    freebytes(x->table, x->table_length * sizeof(float));
    freebytes(x->bitshuffle, (x->N * 2) * sizeof(int));
    freebytes(x->trigland, (x->N * 2) * sizeof(float));
  free(x);
}
/**************************************************/
void fftease_obank_initialize ( t_oscbank *x, float lo_freq, float hi_freq, int overlap,
                                int R, int vector_size, int N)
{
  int i;

  x->overlap = overlap;

  //  x = t_getbytes( sizeof(t_oscbank) ); // CRASH!!

  x->R = R;
  x->vector_size = vector_size;
  x->N = N;
  x->Nw = x->N;
  x->N2 = (x->N)>>1;
  x->Nw2 = (x->Nw)>>1;
  x->in_count = -(x->Nw);
  x->table_length = OSCBANK_TABLE_LENGTH ;
  // x->topfreq = OSCBANK_DEFAULT_TOPFREQ ;

  x->user_lofreq = lo_freq;
  x->user_hifreq = hi_freq;

  x->synthesis_threshold = .000001;
  x->table_si = (float) x->table_length/ (float) x->R;
  x->Wanal = (float *) getbytes(x->Nw * sizeof(float));
  x->Wsyn = (float *) getbytes(x->Nw * sizeof(float));
  x->Hwin = (float *) getbytes(x->Nw * sizeof(float));
  x->complex_spectrum = (float *) getbytes(x->N * sizeof(float));
  x->interleaved_spectrum = (float *) getbytes((x->N + 2) * sizeof(float));
  x->input_buffer = (float *) getbytes(x->Nw * sizeof(float));
  x->output_buffer = (float *) getbytes(x->Nw * sizeof(float));
  x->c_lastphase_in = (float *) getbytes((x->N2+1) * sizeof(float));
  x->c_lastphase_out = (float *) getbytes((x->N2+1) * sizeof(float));
  x->lastamp = (float *) getbytes((x->N+1) * sizeof(float));
  x->lastfreq = (float *) getbytes((x->N+1) * sizeof(float));
  x->index = (float *) getbytes((x->N+1) * sizeof(float) );
  x->table = (float *) getbytes(x->table_length * sizeof(float));
  x->bitshuffle = (int *) getbytes((x->N * 2) * sizeof(int));
  x->trigland = (float *) getbytes((x->N * 2) * sizeof(float));

  x->mult = 1. / (float) x->N;

  for( i = 0; i < x->N2 + 1; i++) {
    x->c_lastphase_in[i] = x->c_lastphase_out[i] = 0.0;
  }

  for( i = 0; i < x->N + 1; i++) {
    x->lastamp[i] = x->lastfreq[i] = x->index[i] = 0.0;
  }

  for( i = 0; i < x->Nw; i++ ) {
    x->input_buffer[i] = x->output_buffer[i] = 0.0;
  }

  init_rdft( x->N, x->bitshuffle, x->trigland);
  makehanning( x->Hwin, x->Wanal, x->Wsyn, x->Nw, x->N, x->vector_size, 0);


  x->c_fundamental =  (float) x->R/(float)x->N ;
  x->c_factor_in =  (float) x->R/((float)x->vector_size * TWOPI);
  x->c_factor_out = 1.0 / x->c_factor_in;



  if( x->user_hifreq < x->c_fundamental ) {
    x->user_hifreq = OSCBANK_DEFAULT_TOPFREQ ;
  }

  x->hi_bin = 1;
  x->curfreq = 0;
  while( x->curfreq < x->user_hifreq ) {
    ++(x->hi_bin);
    x->curfreq += x->c_fundamental ;
  }

  x->lo_bin = 0;
  x->curfreq = 0;
  while( x->curfreq < x->user_lofreq ) {
    ++(x->lo_bin);
    x->curfreq += x->c_fundamental ;
  }

  if( x->hi_bin > x->N2)
    x->hi_bin = x->N2 ;

  for ( i = 0; i < x->table_length; i++ ) {
    x->table[i] = (float) x->N * cos(  (float)i * TWOPI / (float)x->table_length );
  }

  x->P = 1.0 ;
  x->i_vector_size = 1. / x->vector_size;
  x->pitch_increment = x->P * x->table_length/x->R;
  /*
    post("*** oscbank OO ***");
    post("initialized oscbank!");
    post("synthesizing %d bins", x->hi_bin - x->lo_bin);
    post("FFTsize %d, overlap %d", x->N, x->overlap);
    post("vector size %d, Nw %d", x->vector_size, x->Nw);
    post("*** initialization done! ***");
  */
}
/**************************************************/
void  fftease_obank_topfreq( t_oscbank *x, float topfreq )
{
  if( topfreq < x->c_fundamental ) {
    topfreq = OSCBANK_DEFAULT_TOPFREQ ;
  }

  x->hi_bin = 1;
  x->curfreq = 0;
  while( x->curfreq < topfreq ) {
    ++(x->hi_bin);
    x->curfreq += x->c_fundamental ;
  }
  if( x->hi_bin > x->N2)
    x->hi_bin = x->N2 ;
}
/**************************************************/
void  fftease_obank_bottomfreq( t_oscbank *x, float bottomfreq )
{


  x->lo_bin = 0;
  x->curfreq = 0;
  while( x->curfreq < bottomfreq ) {
    ++(x->lo_bin);
    x->curfreq += x->c_fundamental ;
  }

}
/**************************************************/
void  fftease_obank_analyze( t_oscbank *x )
{
  fold( x->input_buffer, x->Wanal, x->Nw, x->complex_spectrum, x->N, x->in_count );

  rdft( x->N, 1, x->complex_spectrum, x->bitshuffle, x->trigland );

  convert( x->complex_spectrum, x->interleaved_spectrum, x->N2, x->c_lastphase_in,
           x->c_fundamental, x->c_factor_in );

}
/**************************************************/
void fftease_shiftin( t_oscbank *x, float *input )
{
  int i;
  int vector_size = x->vector_size;
  int Nw = x->Nw;
  float *input_buffer = x->input_buffer;

  for ( i = 0 ; i < (Nw - vector_size) ; i++ ) {
    input_buffer[i] = input_buffer[i + vector_size];
  }
  for ( i = (Nw - vector_size) ; i < Nw; i++ ) {
    input_buffer[i] = *input++;
  }

}
/**************************************************/
void fftease_shiftout( t_oscbank *x, float *output )
{
  int i;
  int vector_size = x->vector_size;
  int Nw = x->Nw;
  float *output_buffer = x->output_buffer;
  float mult = x->mult;

  for ( i = 0; i < vector_size; i++ ) {
    *output++ = output_buffer[i] * mult;
  }
  for ( i = 0; i < Nw - vector_size; i++ ) {
    output_buffer[i] = output_buffer[i + vector_size];
  }
  for ( i = Nw - vector_size; i < Nw; i++ ) {
    output_buffer[i] = 0.;
  }

  x->in_count += vector_size;
}
/**************************************************/
void fftease_obank_synthesize( t_oscbank *x )
{
  int amp, chan, freq;
  float    a,ainc,f,finc,address;
  int n;

  float synthesis_threshold = x->synthesis_threshold;
  float *lastfreq = x->lastfreq;
  float *lastamp = x->lastamp;
  int table_length = x->table_length;
  float *output_buffer = x->output_buffer;
  int vector_size = x->vector_size;
  float i_vector_size = x->i_vector_size;
  int lo_bin = x->lo_bin;
  int hi_bin = x->hi_bin;
  float *interleaved_spectrum = x->interleaved_spectrum;
  float pitch_increment = x->pitch_increment;
  float *index = x->index;
  float *table = x->table;

  for ( chan = lo_bin; chan < hi_bin; chan++ ) {

    freq = ( amp = ( chan << 1 ) ) + 1;
    if ( interleaved_spectrum[amp] > synthesis_threshold ) {
      interleaved_spectrum[freq] *= pitch_increment;
      finc = ( interleaved_spectrum[freq] - ( f = lastfreq[chan] ) ) * i_vector_size;
      ainc = ( interleaved_spectrum[amp] - ( a = lastamp[chan] ) ) * i_vector_size;
      address = index[chan];
      for ( n = 0; n < vector_size; n++ ) {
        output_buffer[n] += a*table[ (int) address ];

        address += f;
        while ( address >= table_length )
          address -= table_length;
        while ( address < 0 )
          address += table_length;
        a += ainc;
        f += finc;
      }
      lastfreq[chan] = interleaved_spectrum[freq];
      lastamp[chan] = interleaved_spectrum[amp];
      index[chan] = address;
    }
  }
}
////////////////////////
void init_rdft(int n, int *ip, float *w)
{

  int nw,
    nc;

  void  makewt(int nw, int *ip, float *w);
  void  makect(int nc, int *ip, float *c);

  nw = n >> 2;
  makewt(nw, ip, w);

  nc = n >> 2;
  makect(nc, ip, w + nw);

  return;
}


void rdft(int n, int isgn, float *a, int *ip, float *w)
{

  int   j,
    nw,
    nc;

  float   xi;

  void    bitrv2(int n, int *ip, float *a),
    cftsub(int n, float *a, float *w),
    rftsub(int n, float *a, int nc, float *c);


  nw = ip[0];
  nc = ip[1];

  if (isgn < 0) {
    a[1] = 0.5 * (a[1] - a[0]);
    a[0] += a[1];

    for (j = 3; j <= n - 1; j += 2) {
      a[j] = -a[j];
    }

    if (n > 4) {
      rftsub(n, a, nc, w + nw);
      bitrv2(n, ip + 2, a);
    }

    cftsub(n, a, w);

    for (j = 1; j <= n - 1; j += 2) {
      a[j] = -a[j];
    }
  }

  else {

    if (n > 4) {
      bitrv2(n, ip + 2, a);
    }

    cftsub(n, a, w);

    if (n > 4) {
      rftsub(n, a, nc, w + nw);
    }

    xi = a[0] - a[1];
    a[0] += a[1];
    a[1] = xi;
  }
}


void bitrv2(int n, int *ip, float *a)
{
  int j, j1, k, k1, l, m, m2;
  float xr, xi;

  ip[0] = 0;
  l = n;
  m = 1;

  while ((m << 2) < l) {
    l >>= 1;
    for (j = 0; j <= m - 1; j++) {
      ip[m + j] = ip[j] + l;
    }
    m <<= 1;
  }

  if ((m << 2) > l) {

    for (k = 1; k <= m - 1; k++) {

      for (j = 0; j <= k - 1; j++) {
        j1 = (j << 1) + ip[k];
        k1 = (k << 1) + ip[j];
        xr = a[j1];
        xi = a[j1 + 1];
        a[j1] = a[k1];
        a[j1 + 1] = a[k1 + 1];
        a[k1] = xr;
        a[k1 + 1] = xi;
      }
    }
  }

  else {
    m2 = m << 1;

    for (k = 1; k <= m - 1; k++) {

      for (j = 0; j <= k - 1; j++) {
        j1 = (j << 1) + ip[k];
        k1 = (k << 1) + ip[j];
        xr = a[j1];
        xi = a[j1 + 1];
        a[j1] = a[k1];
        a[j1 + 1] = a[k1 + 1];
        a[k1] = xr;
        a[k1 + 1] = xi;
        j1 += m2;
        k1 += m2;
        xr = a[j1];
        xi = a[j1 + 1];
        a[j1] = a[k1];
        a[j1 + 1] = a[k1 + 1];
        a[k1] = xr;
        a[k1 + 1] = xi;
      }
    }
  }
}


void cftsub(int n, float *a, float *w)
{
  int j, j1, j2, j3, k, k1, ks, l, m;
  float wk1r, wk1i, wk2r, wk2i, wk3r, wk3i;
  float x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;

  l = 2;

  while ((l << 1) < n) {
    m = l << 2;

    for (j = 0; j <= l - 2; j += 2) {
      j1 = j + l;
      j2 = j1 + l;
      j3 = j2 + l;
      x0r = a[j] + a[j1];
      x0i = a[j + 1] + a[j1 + 1];
      x1r = a[j] - a[j1];
      x1i = a[j + 1] - a[j1 + 1];
      x2r = a[j2] + a[j3];
      x2i = a[j2 + 1] + a[j3 + 1];
      x3r = a[j2] - a[j3];
      x3i = a[j2 + 1] - a[j3 + 1];
      a[j] = x0r + x2r;
      a[j + 1] = x0i + x2i;
      a[j2] = x0r - x2r;
      a[j2 + 1] = x0i - x2i;
      a[j1] = x1r - x3i;
      a[j1 + 1] = x1i + x3r;
      a[j3] = x1r + x3i;
      a[j3 + 1] = x1i - x3r;
    }

    if (m < n) {
      wk1r = w[2];

      for (j = m; j <= l + m - 2; j += 2) {
        j1 = j + l;
        j2 = j1 + l;
        j3 = j2 + l;
        x0r = a[j] + a[j1];
        x0i = a[j + 1] + a[j1 + 1];
        x1r = a[j] - a[j1];
        x1i = a[j + 1] - a[j1 + 1];
        x2r = a[j2] + a[j3];
        x2i = a[j2 + 1] + a[j3 + 1];
        x3r = a[j2] - a[j3];
        x3i = a[j2 + 1] - a[j3 + 1];
        a[j] = x0r + x2r;
        a[j + 1] = x0i + x2i;
        a[j2] = x2i - x0i;
        a[j2 + 1] = x0r - x2r;
        x0r = x1r - x3i;
        x0i = x1i + x3r;
        a[j1] = wk1r * (x0r - x0i);
        a[j1 + 1] = wk1r * (x0r + x0i);
        x0r = x3i + x1r;
        x0i = x3r - x1i;
        a[j3] = wk1r * (x0i - x0r);
        a[j3 + 1] = wk1r * (x0i + x0r);
      }

      k1 = 1;
      ks = -1;

      for (k = (m << 1); k <= n - m; k += m) {
        k1++;
        ks = -ks;
        wk1r = w[k1 << 1];
        wk1i = w[(k1 << 1) + 1];
        wk2r = ks * w[k1];
        wk2i = w[k1 + ks];
        wk3r = wk1r - 2 * wk2i * wk1i;
        wk3i = 2 * wk2i * wk1r - wk1i;

        for (j = k; j <= l + k - 2; j += 2) {
          j1 = j + l;
          j2 = j1 + l;
          j3 = j2 + l;
          x0r = a[j] + a[j1];
          x0i = a[j + 1] + a[j1 + 1];
          x1r = a[j] - a[j1];
          x1i = a[j + 1] - a[j1 + 1];
          x2r = a[j2] + a[j3];
          x2i = a[j2 + 1] + a[j3 + 1];
          x3r = a[j2] - a[j3];
          x3i = a[j2 + 1] - a[j3 + 1];
          a[j] = x0r + x2r;
          a[j + 1] = x0i + x2i;
          x0r -= x2r;
          x0i -= x2i;
          a[j2] = wk2r * x0r - wk2i * x0i;
          a[j2 + 1] = wk2r * x0i + wk2i * x0r;
          x0r = x1r - x3i;
          x0i = x1i + x3r;
          a[j1] = wk1r * x0r - wk1i * x0i;
          a[j1 + 1] = wk1r * x0i + wk1i * x0r;
          x0r = x1r + x3i;
          x0i = x1i - x3r;
          a[j3] = wk3r * x0r - wk3i * x0i;
          a[j3 + 1] = wk3r * x0i + wk3i * x0r;
        }
      }
    }

    l = m;
  }

  if (l < n) {

    for (j = 0; j <= l - 2; j += 2) {
      j1 = j + l;
      x0r = a[j] - a[j1];
      x0i = a[j + 1] - a[j1 + 1];
      a[j] += a[j1];
      a[j + 1] += a[j1 + 1];
      a[j1] = x0r;
      a[j1 + 1] = x0i;
    }
  }
}


void rftsub(int n, float *a, int nc, float *c)
{
  int j, k, kk, ks;
  float wkr, wki, xr, xi, yr, yi;

  ks = (nc << 2) / n;
  kk = 0;

  for (k = (n >> 1) - 2; k >= 2; k -= 2) {
    j = n - k;
    kk += ks;
    wkr = 0.5 - c[kk];
    wki = c[nc - kk];
    xr = a[k] - a[j];
    xi = a[k + 1] + a[j + 1];
    yr = wkr * xr - wki * xi;
    yi = wkr * xi + wki * xr;
    a[k] -= yr;
    a[k + 1] -= yi;
    a[j] += yr;
    a[j + 1] -= yi;
  }
}


void makewt(int nw, int *ip, float *w)
{
  void bitrv2(int n, int *ip, float *a);
  int nwh, j;
  float delta, x, y;

  ip[0] = nw;
  ip[1] = 1;
  if (nw > 2) {
    nwh = nw >> 1;
    delta = atan(1.0) / nwh;
    w[0] = 1;
    w[1] = 0;
    w[nwh] = cos(delta * nwh);
    w[nwh + 1] = w[nwh];
    for (j = 2; j <= nwh - 2; j += 2) {
      x = cos(delta * j);
      y = sin(delta * j);
      w[j] = x;
      w[j + 1] = y;
      w[nw - j] = y;
      w[nw - j + 1] = x;
    }
    bitrv2(nw, ip + 2, w);
  }
}


void makect(int nc, int *ip, float *c)
{
  int nch, j;
  float delta;

  ip[1] = nc;
  if (nc > 1) {
    nch = nc >> 1;
    delta = atan(1.0) / nch;
    c[0] = 0.5;
    c[nch] = 0.5 * cos(delta * nch);
    for (j = 1; j <= nch - 1; j++) {
      c[j] = 0.5 * cos(delta * j);
      c[nc - j] = 0.5 * sin(delta * j);
    }
  }
}
void convert(float *S, float *C, int N2, float *lastphase, float fundamental, float factor )
{
  float   phase,
    phasediff;
  int     real,
    imag,
    amp,
    freq;
  float   a,
    b;
  int     i;

  /*  float myTWOPI, myPI; */
  /*  double sin(), cos(), atan(), hypot();*/

  /*  myTWOPI = 8.*atan(1.);
      myPI = 4.*atan(1.); */


  for ( i = 0; i <= N2; i++ ) {
    imag = freq = ( real = amp = i<<1 ) + 1;
    a = ( i == N2 ? S[1] : S[real] );
    b = ( i == 0 || i == N2 ? 0. : S[imag] );

    C[amp] = hypot( a, b );
    if ( C[amp] == 0. )
      phasediff = 0.;
    else {
      phasediff = ( phase = -atan2( b, a ) ) - lastphase[i];
      lastphase[i] = phase;

      while ( phasediff > PI )
        phasediff -= TWOPI;
      while ( phasediff < -PI )
        phasediff += TWOPI;
    }
    C[freq] = phasediff*factor + i*fundamental;
  }
}
void fold( float *I, float *W, int Nw, float *O, int N, int n )
{
  int i;

  for ( i = 0; i < N; i++ )
    O[i] = 0.;

  while ( n < 0 )
    n += N;
  n %= N;
  for ( i = 0; i < Nw; i++ ) {
    O[n] += I[i]*W[i];
    if ( ++n == N )
      n = 0;
  }
}

void makehanning( float *H, float *A, float *S, int Nw, int N, int I, int odd )
{
  int i;
  float sum ;


  if (odd) {
    for ( i = 0 ; i < Nw ; i++ )
      H[i] = A[i] = S[i] = sqrt(0.5 * (1. + cos(PI + TWOPI * i / (Nw - 1))));
  }

  else {

    for ( i = 0 ; i < Nw ; i++ )
      H[i] = A[i] = S[i] = 0.5 * (1. + cos(PI + TWOPI * i / (Nw - 1)));

  }

  if ( Nw > N ) {
    float x ;

    x = -(Nw - 1)/2. ;
    for ( i = 0 ; i < Nw ; i++, x += 1. )
      if ( x != 0. ) {
        A[i] *= N*sin( PI*x/N )/(PI*x) ;
        if ( I )
          S[i] *= I*sin( PI*x/I )/(PI*x) ;
      }
  }
  for ( sum = i = 0 ; i < Nw ; i++ )
    sum += A[i] ;

  for ( i = 0 ; i < Nw ; i++ ) {
    float afac = 2./sum ;
    float sfac = Nw > N ? 1./afac : afac ;
    A[i] *= afac ;
    S[i] *= sfac ;
  }

  if ( Nw <= N && I ) {
    for ( sum = i = 0 ; i < Nw ; i += I )
      sum += S[i]*S[i] ;
    for ( sum = 1./sum, i = 0 ; i < Nw ; i++ )
      S[i] *= sum ;
  }
}
