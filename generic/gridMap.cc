// Mapping between MILC and Grid types

#include <Grid/Grid.h>

#include "../include/mGrid/mGrid_internal.h"
#include "../include/mGrid/mGrid.h"
#include "../include/mGrid/mGrid_assert.h"

extern "C" {
#include "generic_includes.h"
#include "../include/openmp_defs.h"
}


using namespace Grid;
using namespace Grid::QCD;
using namespace std;

// extern GridCartesian         *CGrid;
// extern GridRedBlackCartesian *RBGrid;
extern std::vector<int> squaresize;

static void
indexToCoords(uint64_t idx, std::vector<int> &x){

  // Gets the lattice coordinates from the MILC index
  get_coords(x.data(), this_node, idx);
  // For Grid, we need the coordinates within the sublattice hypercube for the current MPI rank
  // NOTE: Would be better to provide a get_subl_coords() in MILC layout_*.c
  for(int i = 0; i < 4; i++)
    x[i] %= squaresize[i];

  //printf("Converted %d to %d %d %d %d\n", idx, x[0], x[1], x[2], x[3]); fflush(stdout);
}


// Create the color vector interface object

template<typename ImprovedStaggeredFermion>
static struct GRID_ColorVector_struct<ImprovedStaggeredFermion> *
create_V(int milc_parity, GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb){

  GridCartesian *CGrid = grid_full->grid;
  GridRedBlackCartesian *RBGrid = grid_rb->grid;

  struct GRID_ColorVector_struct<ImprovedStaggeredFermion> *out;

  out = (struct GRID_ColorVector_struct<ImprovedStaggeredFermion> *) 
    malloc(sizeof(struct GRID_ColorVector_struct<ImprovedStaggeredFermion>));
  GRID_ASSERT( out != NULL, GRID_MEM_ERROR );


  switch (milc_parity)
    {
    case EVEN:
      out->cv = new typename ImprovedStaggeredFermion::FermionField(RBGrid);
      GRID_ASSERT(out->cv != NULL, GRID_MEM_ERROR);
      out->cv->checkerboard = Even;
      break;

    case ODD:
      out->cv = new typename ImprovedStaggeredFermion::FermionField(RBGrid);
      GRID_ASSERT(out->cv != NULL, GRID_MEM_ERROR);
      out->cv->checkerboard = Odd;
      break;

    case EVENANDODD:
      out->cv = new typename ImprovedStaggeredFermion::FermionField(CGrid);
      GRID_ASSERT(out->cv != NULL, GRID_MEM_ERROR);
      break;
      
    default:
      break;
    }

  return out;
}

// Create the block color vector interface object

template<typename ImprovedStaggeredFermion5D>
static struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D> *
create_nV(int n, int milc_parity,
	  GRID_5Dgrid *grid_5D, GRID_5DRBgrid *grid_5Drb, 
	  GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb ){

  GridCartesian *CGrid = grid_full->grid;
  GridRedBlackCartesian *RBGrid = grid_rb->grid;
  GridCartesian *FCGrid = grid_5D->grid;
  GridRedBlackCartesian *FRBGrid = grid_5Drb->grid;

  struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D> *out;
  out = (struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D> *) 
    malloc(sizeof(struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D>));
  GRID_ASSERT( out != NULL, GRID_MEM_ERROR );

  if(milc_parity == EVEN || milc_parity == ODD){
    std::cout << "Constructing 5D field\n" << std::flush;
    out->cv = new typename ImprovedStaggeredFermion5D::FermionField(FRBGrid);
    GRID_ASSERT(out->cv != NULL, GRID_MEM_ERROR);
    out->cv->checkerboard = milc_parity == EVEN ? Even : Odd ;
  } else {
    out->cv = new typename ImprovedStaggeredFermion5D::FermionField(FCGrid);
    GRID_ASSERT(out->cv != NULL, GRID_MEM_ERROR);
  }

  return out;
}

// free color vector
template<typename ImprovedStaggeredFermion>
static void 
destroy_V( struct GRID_ColorVector_struct<ImprovedStaggeredFermion> *V ){

  if (V->cv != NULL) delete V->cv;
  if (V != NULL) free(V);

  return;
}

// free block color vector
template<typename ImprovedStaggeredFermion5D>
static void 
destroy_nV( struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D> *V ){

  if (V->cv != NULL) {
    delete V->cv;
  }
  if (V != NULL) free(V);

  return;
}

// Create the color vector interface object
// and Map a MILC color vector field from MILC to Grid layout
// Precision conversion takes place in the copies if need be

template<typename ImprovedStaggeredFermion, typename ColourVector, typename Complex>
static struct GRID_ColorVector_struct<ImprovedStaggeredFermion> *
create_V_from_vec( su3_vector *src, int milc_parity,
		   GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb){

  struct GRID_ColorVector_struct<ImprovedStaggeredFermion> *out;

  out = create_V<ImprovedStaggeredFermion>(milc_parity, grid_full, grid_rb);

  int loopend= (milc_parity)==EVEN ? even_sites_on_node : sites_on_node ;
  int loopstart=((milc_parity)==ODD ? even_sites_on_node : 0 );

  auto start = std::chrono::system_clock::now();
  PARALLEL_FOR_LOOP
    for( uint64_t idx = loopstart; idx < loopend; idx++){

      std::vector<int> x(4);
      indexToCoords(idx,x);

      ColourVector cVec;
      for(int col=0; col<Nc; col++){
	cVec._internal._internal._internal[col] = 
	  Complex(src[idx].c[col].real, src[idx].c[col].imag);
      }
      
      pokeLocalSite(cVec, *(out->cv), x);
      
    }
  auto end = std::chrono::system_clock::now();
  auto elapsed = end - start;
  //  std::cout << "Mapped vector field in " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) 
  //	    << "\n" << std::flush;
  
  return out;
}

// Create the blocked color vector interface object
// and Map a set of MILC color vector field from MILC to Grid layout
// Precision conversion takes place in the copies if need be

template<typename ImprovedStaggeredFermion5D, typename ColourVector, typename Complex>
static struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D> *
create_nV_from_vecs( su3_vector *src[], int n, int milc_parity,
		     GRID_5Dgrid *grid_5D, GRID_5DRBgrid *grid_5Drb,
		     GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb ){

  struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D> *out;

  out = create_nV<ImprovedStaggeredFermion5D>(n, milc_parity, grid_5D, grid_5Drb,  grid_full, grid_rb);

  int loopend= (milc_parity)==EVEN ? even_sites_on_node : sites_on_node ;
  int loopstart=((milc_parity)==ODD ? even_sites_on_node : 0 );

  auto start = std::chrono::system_clock::now();
  PARALLEL_FOR_LOOP
    for( uint64_t idx = loopstart; idx < loopend; idx++){

      std::vector<int> x(4);
      indexToCoords(idx,x);
      std::vector<int> x5(1,0);
      for( int d = 0; d < 4; d++ )
	x5.push_back(x[d]);

      for( int j = 0; j < n; j++ ){
	x5[0] = j;
	ColourVector cVec;
	for(int col=0; col<Nc; col++){
	  cVec._internal._internal._internal[col] = 
	    Complex(src[j][idx].c[col].real, src[j][idx].c[col].imag);
	}
	pokeLocalSite(cVec, *(out->cv), x5);
      }
    }
  auto end = std::chrono::system_clock::now();
  auto elapsed = end - start;
  std::cout << "Mapped vector field in " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) 
  	    << "\n" << std::flush;
  
  return out;
}

// Map a color vector field from Grid layout to MILC layout
template<typename ImprovedStaggeredFermion, typename ColourVector>
static void extract_V_to_vec( su3_vector *dest, 
			      struct GRID_ColorVector_struct<ImprovedStaggeredFermion> *src, 
			      int milc_parity ){
  uint64_t idx;

  FORSOMEFIELDPARITY_OMP(idx, milc_parity, )
    {
      std::vector<int> x(4);
      indexToCoords(idx, x);
      ColourVector cVec;

      peekLocalSite(cVec, *(src->cv), x);

      for(int col = 0; col < Nc; col++)
	{
	  dest[idx].c[col].real = cVec._internal._internal._internal[col].real();
	  dest[idx].c[col].imag = cVec._internal._internal._internal[col].imag();
	}
    } END_LOOP_OMP;

  return;
}

// Map a color vector field from Grid layout to MILC layout
template<typename ImprovedStaggeredFermion5D, typename ColourVector>
static void extract_nV_to_vecs( su3_vector *dest[], int n,
				struct GRID_ColorVectorBlock_struct<ImprovedStaggeredFermion5D> *src, 
				int milc_parity ){
  uint64_t idx;

  FORSOMEFIELDPARITY_OMP(idx, milc_parity, )
    {
      std::vector<int> x(4);
      indexToCoords(idx, x);
      std::vector<int> x5(1,0);
      for( int d = 0; d < 4; d++)
	x5.push_back(x[d]);

      for( int j = 0; j < n; j++ ){
	x[0] = j;

	ColourVector cVec;
	peekLocalSite(cVec, *(src->cv), x5);
	
	for(int col = 0; col < Nc; col++)
	  {
	    dest[j][idx].c[col].real = cVec._internal._internal._internal[col].real();
	    dest[j][idx].c[col].imag = cVec._internal._internal._internal[col].imag();
	  }
      }
    } END_LOOP_OMP;

  return;
}

// Copy MILC su3_matrix to Grid vLorentzColourMatrix
// Precision conversion can happen here

template<typename sobj, typename Complex>
static void milcSU3MatrixToGrid(su3_matrix *in, sobj &out){

  for (int mu=0; mu<4; mu++)
    for (int i=0; i<Nc; i++)
      for (int j=0; j<Nc; j++)
	out._internal[mu]._internal._internal[i][j] = Complex(in[mu].e[i][j].real, in[mu].e[i][j].imag);
}

// Map a flattened MILC gauge field (4 matrices per site) to a Grid LatticeGaugeField
template<typename LatticeGaugeField, typename Complex>
static void milcGaugeFieldToGrid(su3_matrix *in, LatticeGaugeField &out){

  typedef typename LatticeGaugeField::vector_object vobj;
  typedef typename vobj::scalar_object sobj;

  GridBase *grid = out._grid;
  int lsites = grid->lSites();
  std::vector<sobj> scalardata(lsites);

  PARALLEL_FOR_LOOP
    for (uint64_t milc_idx = 0; milc_idx < sites_on_node; milc_idx++){
      std::vector<int> x(4);
      indexToCoords(milc_idx, x);
      int grid_idx;
      Lexicographic::IndexFromCoor(x, grid_idx, grid->LocalDimensions());
      milcSU3MatrixToGrid<sobj, Complex>(in + 4*milc_idx, scalardata[grid_idx]);
    }
  
  vectorizeFromLexOrdArray(scalardata, out);
}

template<typename ColourMatrix>
static void dumpGrid(ColourMatrix out){

  for (int i=0; i<Nc; i++){
    for (int j=0; j<Nc; j++)
      std::cout << "(" << out._internal._internal._internal[i][j].real() << "," << 
	out._internal._internal._internal[i][j].imag() << ") ";
    std::cout << "\n";
  }
  std::cout << "\n";
}

// Create asqtad fermion links object from MILC fields
// Precision conversion takes place in the copies if need be

template<typename LatticeGaugeField, typename LatticeColourMatrix, typename Complex>
static struct GRID_FermionLinksAsqtad_struct<LatticeGaugeField>  *
asqtad_create_L_from_MILC( su3_matrix *thn, su3_matrix *fat, 
			   su3_matrix *lng, GRID_4Dgrid *grid_full ){

  GridCartesian *CGrid = grid_full->grid;

  struct GRID_FermionLinksAsqtad_struct<LatticeGaugeField> *out;

  out = (struct GRID_FermionLinksAsqtad_struct<LatticeGaugeField> *) malloc(sizeof(struct GRID_FermionLinksAsqtad_struct<LatticeGaugeField>));
  GRID_ASSERT(out != NULL, GRID_MEM_ERROR);


  out->thnlinks = NULL;  // We don't need this one
  out->fatlinks = new LatticeGaugeField(CGrid);
  out->lnglinks = new LatticeGaugeField(CGrid);
  GRID_ASSERT(out->fatlinks != NULL, GRID_MEM_ERROR);
  GRID_ASSERT(out->lnglinks != NULL, GRID_MEM_ERROR);
  
  auto start = std::chrono::system_clock::now();

  milcGaugeFieldToGrid<LatticeGaugeField, Complex>(fat, *out->fatlinks);
  milcGaugeFieldToGrid<LatticeGaugeField, Complex>(lng, *out->lnglinks);

  auto end = std::chrono::system_clock::now();
  auto elapsed = end - start;
  std::cout << "Mapped gauge fields in " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) 
	    << "\n" << std::flush;
  return out;
}

  // free aasqtad fermion links
template<typename LatticeGaugeField>
static void  
asqtad_destroy_L( struct GRID_FermionLinksAsqtad_struct<LatticeGaugeField> *Link ){

  if (Link == NULL) return;
  
  if (Link->thnlinks != NULL) delete Link->thnlinks;
  if (Link->fatlinks != NULL) delete Link->fatlinks;
  if (Link->lnglinks != NULL) delete Link->lnglinks;
  
  delete Link;
  
  Link = NULL;
}

//====================================================================//
// The GRID C API for mapping between MILC and GRID types

// Create a 4D, full-grid wrapper
GRID_4Dgrid *
GRID_create_grid(void){
  std::vector<int> latt_size   = GridDefaultLatt();
  std::vector<int> simd_layout = GridDefaultSimd(Nd,vComplex::Nsimd());
  std::vector<int> mpi_layout  = GridDefaultMpi();

  GridCartesian *CGrid  = new GridCartesian(latt_size,simd_layout,mpi_layout);
  GRID_ASSERT(CGrid != NULL, GRID_MEM_ERROR);
  GRID_4Dgrid *g4D = (GRID_4Dgrid *)malloc(sizeof(struct GRID_4Dgrid_struct));
  GRID_ASSERT(g4D != NULL, GRID_MEM_ERROR);
  g4D->grid = CGrid;
  return g4D;
}

// Create a 4D red-black-grid wrapper
GRID_4DRBgrid *
GRID_create_RBgrid(GRID_4Dgrid *grid_full){
  GridRedBlackCartesian *RBGrid = new GridRedBlackCartesian(grid_full->grid);
  GRID_ASSERT(RBGrid != NULL, GRID_MEM_ERROR);
  GRID_4DRBgrid *g4D = (GRID_4DRBgrid *)malloc(sizeof(struct GRID_4DRBgrid_struct));
  GRID_ASSERT(g4D != NULL, GRID_MEM_ERROR);
  g4D->grid = RBGrid;
  return g4D;
}

// Create a 5D full-grid wrapper
GRID_5Dgrid *
GRID_create_5Dgrid(int n, GRID_4Dgrid *grid_full){
  GRID_5Dgrid *g5D = (GRID_5Dgrid *)malloc(sizeof(struct GRID_5Dgrid_struct));
  GRID_ASSERT(g5D != NULL, GRID_MEM_ERROR);
  GridCartesian *FGrid = SpaceTimeGrid::makeFiveDimGrid(n, grid_full->grid);
  g5D->grid = FGrid;
  return g5D;
}

// Create a 5D RB-grid wrapper
GRID_5DRBgrid *
GRID_create_5DRBgrid(int n, GRID_4Dgrid *grid_full){
  GRID_5DRBgrid *g5D = (GRID_5DRBgrid *)malloc(sizeof(struct GRID_5DRBgrid_struct));
  GRID_ASSERT(g5D != NULL, GRID_MEM_ERROR);
  std::cout << "Constructing 5D grid for " << n << " fields\n" << std::flush;
  auto start = std::chrono::system_clock::now();
  GridRedBlackCartesian *FRBGrid = SpaceTimeGrid::makeFiveDimRedBlackGrid(n, grid_full->grid);
  auto end = std::chrono::system_clock::now();
  auto elapsed = end - start;
  std::cout << "Construct 5D grids in " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) 
	    << "\n" << std::flush;
  g5D->grid = FRBGrid;
  return g5D;
}

void 
GRID_destroy_4Dgrid(GRID_4Dgrid *grid){
  if(grid){
    if(grid->grid)
      delete grid->grid;
    free(grid);
  }
}

void 
GRID_destroy_4DRBgrid(GRID_4DRBgrid *grid){
  if(grid){
    if(grid->grid)
      delete grid->grid;
    free(grid);
  }
}

void 
GRID_destroy_5Dgrid(GRID_5Dgrid *grid){
  if(grid){
    if(grid->grid)
      delete grid->grid;
    free(grid);
  }
}

void 
GRID_destroy_5DRBgrid(GRID_5DRBgrid *grid){
  if(grid){
    if(grid->grid)
      delete grid->grid;
    free(grid);
  }
}

// create color vector
GRID_F3_ColorVector *
GRID_F3_create_V( int milc_parity, GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb ){
  create_V<ImprovedStaggeredFermionF>( milc_parity, grid_full, grid_rb );
}

// create block color vector
GRID_F3_ColorVectorBlock *
GRID_F3_create_nV( int n, int milc_parity, 
		   GRID_5Dgrid *grid_5D, GRID_5DRBgrid *grid_5Drb,
		   GRID_4Dgrid *grid_full,GRID_4DRBgrid *grid_rb ){
  create_nV<ImprovedStaggeredFermion5DF>( n, milc_parity, grid_5D, grid_5Drb, grid_full, grid_rb );
}

// create color vector
GRID_D3_ColorVector *
GRID_D3_create_V( int milc_parity, GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb ){
  create_V<ImprovedStaggeredFermionD>( milc_parity, grid_full, grid_rb );
}

// ceate block color vector
GRID_D3_ColorVectorBlock *
GRID_D3_create_nV( int n, int milc_parity,
                   GRID_5Dgrid *grid_5D, GRID_5DRBgrid *grid_5Drb, 
                   GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb ){
  create_nV<ImprovedStaggeredFermion5DD>( n, milc_parity, grid_5D, grid_5Drb, grid_full, grid_rb );
}

// free color vector
void  
GRID_F3_destroy_V( GRID_F3_ColorVector *gcv ){
  destroy_V<ImprovedStaggeredFermionF>( gcv );
}

// free block color vector
void  
GRID_F3_destroy_nV( GRID_F3_ColorVectorBlock *gcv ){
  destroy_nV<ImprovedStaggeredFermion5DF>( gcv );
}

// free color vector
void  
GRID_D3_destroy_V( GRID_D3_ColorVector *gcv ){
  destroy_V<ImprovedStaggeredFermionD>( gcv );
}

// free block color vector
void  
GRID_D3_destroy_nV( GRID_D3_ColorVectorBlock *gcv ){
  destroy_nV<ImprovedStaggeredFermion5DD>( gcv );
}

// Map a Dirac vector field from MILC layout to GRID layout
GRID_F3_ColorVector  *
GRID_F3_create_V_from_vec( su3_vector *src, int milc_parity,
			   GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb){
  return create_V_from_vec<ImprovedStaggeredFermionF, ColourVectorF, ComplexF>( src, milc_parity,
								grid_full, grid_rb);
}
  
// Map a Dirac vector field from MILC layout to GRID layout
GRID_F3_ColorVectorBlock *
GRID_F3_create_nV_from_vecs( su3_vector *src[], int n, int milc_parity,
			     GRID_5Dgrid *grid_5D, GRID_5DRBgrid *grid_5Drb, 
			     GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb ){
  return create_nV_from_vecs<ImprovedStaggeredFermion5DF, ColourVectorF, ComplexF>( src, n, milc_parity,
						    grid_5D, grid_5Drb, grid_full,grid_rb );
}
  
// Map a Dirac vector field from MILC layout to QPhiX layout
GRID_D3_ColorVector *
GRID_D3_create_V_from_vec( su3_vector *src, int milc_parity,
			   GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb){
  return create_V_from_vec<ImprovedStaggeredFermionD, ColourVectorD, ComplexD>( src, milc_parity,
								grid_full, grid_rb);
}
  
// Map a blocked Dirac vector field from MILC layout to QPhiX layout
GRID_D3_ColorVectorBlock *
GRID_D3_create_nV_from_vecs( su3_vector *src[], int n, int milc_parity,
                             GRID_5Dgrid *grid_5D, GRID_5DRBgrid *grid_5Drb,
                             GRID_4Dgrid *grid_full, GRID_4DRBgrid *grid_rb ){
  return create_nV_from_vecs<ImprovedStaggeredFermion5DD, ColourVectorD, ComplexD>( src, n, milc_parity,
							    grid_5D, grid_5Drb, grid_full, grid_rb );
}
  
// Map a color vector field from GRID layout to MILC layout
void 
GRID_F3_extract_V_to_vec( su3_vector *dest, GRID_F3_ColorVector *gcv, int milc_parity ){
  extract_V_to_vec<ImprovedStaggeredFermionF, ColourVectorF>( dest, gcv, milc_parity );
}

// Map a block color vector field from GRID layout to MILC layout
void 
GRID_F3_extract_nV_to_vecs( su3_vector *dest[], int n, GRID_F3_ColorVectorBlock *gcv, int milc_parity ){
  extract_nV_to_vecs<ImprovedStaggeredFermion5DF, ColourVectorF>( dest, n, gcv, milc_parity );
}

// Map a color vector field from GRID layout to MILC layout
void 
GRID_D3_extract_V_to_vec( su3_vector *dest, GRID_D3_ColorVector *gcv, int milc_parity ){
  extract_V_to_vec<ImprovedStaggeredFermionD, ColourVectorD>( dest, gcv, milc_parity );
}

// Map a block color vector field from GRID layout to MILC layout
void 
GRID_D3_extract_nV_to_vecs( su3_vector *dest[], int n, GRID_D3_ColorVectorBlock *gcv, int milc_parity ){
  extract_nV_to_vecs<ImprovedStaggeredFermion5DD, ColourVectorD>( dest, n, gcv, milc_parity );
}

// create asqtad fermion links from MILC
GRID_F3_FermionLinksAsqtad  *
GRID_F3_asqtad_create_L_from_MILC( su3_matrix *thnlinks, su3_matrix *fatlinks, su3_matrix *lnglinks, 
				   GRID_4Dgrid *grid_full ){
  return asqtad_create_L_from_MILC<LatticeGaugeFieldF, LatticeColourMatrixF, ComplexF>( thnlinks, 
                  fatlinks, lnglinks, grid_full );
}

// create asqtad fermion links from MILC
GRID_D3_FermionLinksAsqtad  *
GRID_D3_asqtad_create_L_from_MILC( su3_matrix *thnlinks, su3_matrix *fatlinks, su3_matrix *lnglinks, 
				   GRID_4Dgrid *grid_full ){
  return asqtad_create_L_from_MILC<LatticeGaugeFieldD, LatticeColourMatrixD, ComplexD>( thnlinks, 
		   fatlinks, lnglinks, grid_full );
}

// free asqtad fermion links
void  
GRID_F3_asqtad_destroy_L( GRID_F3_FermionLinksAsqtad *gl ){
  asqtad_destroy_L<LatticeGaugeFieldF>( gl );
}

// free asqtad fermion links
void  
GRID_D3_asqtad_destroy_L( GRID_D3_FermionLinksAsqtad *gl ){
  asqtad_destroy_L<LatticeGaugeFieldD>( gl );
}


