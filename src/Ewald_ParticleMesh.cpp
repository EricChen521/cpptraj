#ifdef LIBPME
#include <algorithm> // copy/fill
//#incl ude <memory> // unique_ptr
#include "Ewald_ParticleMesh.h"
#include "CpptrajStdio.h"
//#include "helpme.h"
#include <chrono>
#include <ctime>

typedef helpme::Matrix<double> Mat;

/// CONSTRUCTOR
Ewald_ParticleMesh::Ewald_ParticleMesh() : order_(6)
{
  nfft_[0] = -1;
  nfft_[1] = -1;
  nfft_[2] = -1;
}

/** \return true if given number is a product of powers of 2, 3, or 5. */
static inline bool check_prime_factors(int nIn) {
  if (nIn == 1) return true;
  int NL = nIn;
  int NQ;
  // First divide down by 2
  while (NL > 0) {
    NQ = NL / 2;
    if (NQ * 2 != NL) break;
    if (NQ == 1) return true;
    NL = NQ;
  }
  // Next try 3
  while (NL > 0) {
    NQ = NL / 3;
    if (NQ * 3 != NL) break;
    if (NQ == 1) return true;
    NL = NQ;
  }
  // Last try 5
  while (NL > 0) {
    NQ = NL / 5;
    if (NQ * 5 != NL) break;
    if (NQ == 1) return true;
    NL = NQ;
  }
  return false;
}

/** Compute the ceiling of len that is also a product of powers of 2, 3, 5.
  * Use check_prime_factors to get the smallest integer greater or equal
  * than len which is decomposable into powers of 2, 3, 5.
  */
int Ewald_ParticleMesh::ComputeNFFT(double len) {
  int mval = (int)len - 1;
  for (int i = 0; i < 100; i++) {
    mval += 1;
    // Sanity check
    if (mval < 1) {
      mprinterr("Error: Bad box length %g, cannot get NFFT value.\n", len);
      return 0;
    }
    if (check_prime_factors(mval))
      return mval;
  }
  mprinterr("Error: Failed to get good FFT array size for length %g Ang.\n", len);
  return 0;
}

/** Given a box, determine number of FFT grid points in each dimension. */
int Ewald_ParticleMesh::DetermineNfft(int& nfft1, int& nfft2, int& nfft3, Box const& boxIn) const
{
   if (nfft1 < 1) {
    // Need even dimension for X direction
    nfft1 = ComputeNFFT( (boxIn.BoxX() + 1.0) * 0.5 );
    nfft1 *= 2;
  }
  if (nfft2 < 1)
    nfft2 = ComputeNFFT( boxIn.BoxY() );
  if (nfft3 < 1)
    nfft3 = ComputeNFFT( boxIn.BoxZ() );

  if (nfft1 < 1 || nfft2 < 1 || nfft3 < 1) {
    mprinterr("Error: Bad NFFT values: %i %i %i\n", nfft1, nfft2, nfft3);
    return 1;
  }
  if (debug_ > 0) mprintf("DEBUG: NFFTs: %i %i %i\n", nfft1, nfft2, nfft3);

  return 0;
}

/** Set up PME parameters. */
int Ewald_ParticleMesh::Init(Box const& boxIn, double cutoffIn, double dsumTolIn,
                    double ew_coeffIn, double lw_coeffIn, double switch_widthIn,
                    double skinnbIn, double erfcTableDxIn, 
                    int orderIn, int debugIn, const int* nfftIn)
{
  if (CheckInput(boxIn, debugIn, cutoffIn, dsumTolIn, ew_coeffIn, lw_coeffIn, switch_widthIn,
                 erfcTableDxIn, skinnbIn))
    return 1;
  if (nfftIn != 0)
    std::copy(nfftIn, nfftIn+3, nfft_);
  else
    std::fill(nfft_, nfft_+3, -1);
  order_ = orderIn;

  // Set defaults if necessary
  if (order_ < 1) order_ = 6;

  mprintf("\tParticle Mesh Ewald params:\n");
  mprintf("\t  Cutoff= %g   Direct Sum Tol= %g   Ewald coeff.= %g  NB skin= %g\n",
          cutoff_, dsumTol_, ew_coeff_, skinnbIn);
  if (lw_coeff_ > 0.0)
    mprintf("\t  LJ Ewald coeff.= %g\n", lw_coeff_);
  if (switch_width_ > 0.0)
    mprintf("\t  LJ switch width= %g\n", switch_width_);
  mprintf("\t  Bspline order= %i\n", order_);
  mprintf("\t  Erfc table dx= %g, size= %zu\n", erfcTableDx_, erfc_table_.size()/4);
  mprintf("\t ");
  for (int i = 0; i != 3; i++)
    if (nfft_[i] == -1)
      mprintf(" NFFT%i=auto", i+1);
    else
      mprintf(" NFFT%i=%i", i+1, nfft_[i]);
  mprintf("\n");

  // Set up pair list
  Matrix_3x3 ucell, recip;
  boxIn.ToRecip(ucell, recip);
  Vec3 recipLengths = boxIn.RecipLengths(recip);
  if (Setup_Pairlist(boxIn, recipLengths, skinnbIn)) return 1;

  return 0;
}

/** Setup PME calculation. */
int Ewald_ParticleMesh::Setup(Topology const& topIn, AtomMask const& maskIn) {
  CalculateCharges(topIn, maskIn);
  // NOTE: These dont need to actually be calculated if the lj ewald coeff
  //       is 0.0, but do it here anyway to avoid segfaults.
  CalculateC6params( topIn, maskIn );
  coordsD_.clear();
  coordsD_.reserve( maskIn.Nselected() * 3);
  SetupExcluded(topIn, maskIn);
  return 0;
}

/*
static inline void PrintM(const char* Title, Mat<double> const& M_)
{
  mprintf("    %s\n",Title);
  mprintf("     %16.10f %16.10f %16.10f\n", M_(0,0), M_(0,1), M_(0,2));
  mprintf("     %16.10f %16.10f %16.10f\n", M_(1,0), M_(1,1), M_(1,2));
  mprintf("     %16.10f %16.10f %16.10f\n", M_(2,0), M_(2,1), M_(2,2));
}*/

// Ewald::Recip_ParticleMesh()
double Ewald_ParticleMesh::Recip_ParticleMesh(Box const& boxIn)
{
  t_recip_.Start();
  // This essentially makes coordsD and chargesD point to arrays.
  Mat coordsD(&coordsD_[0], Charge_.size(), 3);
  Mat chargesD(&Charge_[0], Charge_.size(), 1);
  int nfft1 = nfft_[0];
  int nfft2 = nfft_[1];
  int nfft3 = nfft_[2];
  if ( DetermineNfft(nfft1, nfft2, nfft3, boxIn) ) {
    mprinterr("Error: Could not determine grid spacing.\n");
    return 0.0;
  }
  // Instantiate double precision PME object
  // Args: 1 = Exponent of the distance kernel: 1 for Coulomb
  //       2 = Kappa
  //       3 = Spline order
  //       4 = nfft1
  //       5 = nfft2
  //       6 = nfft3
  //       7 = scale factor to be applied to all computed energies and derivatives thereof
  //       8 = max # threads to use for each MPI instance; 0 = all available threads used.
  // NOTE: Scale factor for Charmm is 332.0716
  // NOTE: The electrostatic constant has been baked into the Charge_ array already.
  //auto pme_object = std::unique_ptr<PMEInstanceD>(new PMEInstanceD());
  pme_object_.setup(1, ew_coeff_, order_, nfft1, nfft2, nfft3, 1.0, 0);
  // Sets the unit cell lattice vectors, with units consistent with those used to specify coordinates.
  // Args: 1 = the A lattice parameter in units consistent with the coordinates.
  //       2 = the B lattice parameter in units consistent with the coordinates.
  //       3 = the C lattice parameter in units consistent with the coordinates.
  //       4 = the alpha lattice parameter in degrees.
  //       5 = the beta lattice parameter in degrees.
  //       6 = the gamma lattice parameter in degrees.
  //       7 = lattice type
  pme_object_.setLatticeVectors(boxIn.BoxX(), boxIn.BoxY(), boxIn.BoxZ(),
                                boxIn.Alpha(), boxIn.Beta(), boxIn.Gamma(),
                                PMEInstanceD::LatticeType::XAligned);
  double erecip = pme_object_.computeERec(0, chargesD, coordsD);

  t_recip_.Stop();
  return erecip;
}

/** The LJ PME reciprocal term. */
double Ewald_ParticleMesh::LJ_Recip_ParticleMesh(Box const& boxIn)
{
  t_recip_.Start();
  int nfft1 = nfft_[0];
  int nfft2 = nfft_[1];
  int nfft3 = nfft_[2];
  if ( DetermineNfft(nfft1, nfft2, nfft3, boxIn) ) {
    mprinterr("Error: Could not determine grid spacing.\n");
    return 0.0;
  }

  Mat coordsD(&coordsD_[0], Charge_.size(), 3);
  Mat cparamD(&Cparam_[0], Cparam_.size(), 1);

  //auto pme_vdw = std::unique_ptr<PMEInstanceD>(new PMEInstanceD());
  pme_vdw_.setup(6, lw_coeff_, order_, nfft1, nfft2, nfft3, -1.0, 0);
  pme_vdw_.setLatticeVectors(boxIn.BoxX(), boxIn.BoxY(), boxIn.BoxZ(),
                             boxIn.Alpha(), boxIn.Beta(), boxIn.Gamma(),
                             PMEInstanceD::LatticeType::XAligned);
  double evdwrecip = pme_vdw_.computeERec(0, cparamD, coordsD);
  t_recip_.Stop();
  return evdwrecip;
}

// Ewald::Recip_ParticleMesh() for GIST to store decomposed recipical energy for every atom 
double Ewald_ParticleMesh::Recip_ParticleMesh_GIST(Box const& boxIn, helpme::Matrix<double>& potential)
{
  t_recip_.Start();
  // This essentially makes coordsD and chargesD point to arrays.
  Mat coordsD(&coordsD_[0], Charge_.size(), 3);
  Mat chargesD(&Charge_[0], Charge_.size(), 1);
  int nfft1 = nfft_[0];
  int nfft2 = nfft_[1];
  int nfft3 = nfft_[2];
  if ( DetermineNfft(nfft1, nfft2, nfft3, boxIn) ) {
    mprinterr("Error: Could not determine grid spacing.\n");
    return 0.0;
  }
  // Instantiate double precision PME object
  // Args: 1 = Exponent of the distance kernel: 1 for Coulomb
  //       2 = Kappa
  //       3 = Spline order
  //       4 = nfft1
  //       5 = nfft2
  //       6 = nfft3
  //       7 = scale factor to be applied to all computed energies and derivatives thereof
  //       8 = max # threads to use for each MPI instance; 0 = all available threads used.
  // NOTE: Scale factor for Charmm is 332.0716
  // NOTE: The electrostatic constant has been baked into the Charge_ array already.
  //auto pme_object = std::unique_ptr<PMEInstanceD>(new PMEInstanceD());
  pme_object_.setup(1, ew_coeff_, order_, nfft1, nfft2, nfft3, 1.0, 0);
  // Sets the unit cell lattice vectors, with units consistent with those used to specify coordinates.
  // Args: 1 = the A lattice parameter in units consistent with the coordinates.
  //       2 = the B lattice parameter in units consistent with the coordinates.
  //       3 = the C lattice parameter in units consistent with the coordinates.
  //       4 = the alpha lattice parameter in degrees.
  //       5 = the beta lattice parameter in degrees.
  //       6 = the gamma lattice parameter in degrees.
  //       7 = lattice type
  pme_object_.setLatticeVectors(boxIn.BoxX(), boxIn.BoxY(), boxIn.BoxZ(),
                                boxIn.Alpha(), boxIn.Beta(), boxIn.Gamma(),
                                PMEInstanceD::LatticeType::XAligned);
  double erecip = pme_object_.computeERec(0, chargesD, coordsD);
  pme_object_.computePRec(0,chargesD,coordsD,coordsD,1,potential);




  t_recip_.Stop();
  return erecip;
}
/** The LJ PME reciprocal term for GIST*/ 
double Ewald_ParticleMesh::LJ_Recip_ParticleMesh_GIST(Box const& boxIn, helpme::Matrix<double>& potential)
{
  t_recip_.Start();
  int nfft1 = nfft_[0];
  int nfft2 = nfft_[1];
  int nfft3 = nfft_[2];
  if ( DetermineNfft(nfft1, nfft2, nfft3, boxIn) ) {
    mprinterr("Error: Could not determine grid spacing.\n");
    return 0.0;
  }

  Mat coordsD(&coordsD_[0], Charge_.size(), 3);
  Mat cparamD(&Cparam_[0], Cparam_.size(), 1);


  //auto pme_vdw = std::unique_ptr<PMEInstanceD>(new PMEInstanceD());
  pme_vdw_.setup(6, lw_coeff_, order_, nfft1, nfft2, nfft3, -1.0, 0);
  pme_vdw_.setLatticeVectors(boxIn.BoxX(), boxIn.BoxY(), boxIn.BoxZ(),
                             boxIn.Alpha(), boxIn.Beta(), boxIn.Gamma(),
                             PMEInstanceD::LatticeType::XAligned);
  double evdwrecip = pme_vdw_.computeERec(0, cparamD, coordsD);
  pme_vdw_.computePRec(0,cparamD,coordsD,coordsD,1,potential);
  t_recip_.Stop();
  return evdwrecip;
}



/** Calculate full nonbonded energy with PME */
int Ewald_ParticleMesh::CalcNonbondEnergy(Frame const& frameIn, AtomMask const& maskIn,
                                      double& e_elec, double& e_vdw)
{
  t_total_.Start();
  Matrix_3x3 ucell, recip;
  double volume = frameIn.BoxCrd().ToRecip(ucell, recip);
  double e_self = Self( volume );
  double e_vdw_lr_correction;

  int retVal = pairList_.CreatePairList(frameIn, ucell, recip, maskIn);
  if (retVal != 0) {
    mprinterr("Error: Grid setup failed.\n");
    return 1;
  }

  // TODO make more efficient
  int idx = 0;
  coordsD_.clear();
  for (AtomMask::const_iterator atm = maskIn.begin(); atm != maskIn.end(); ++atm, ++idx) {
    const double* XYZ = frameIn.XYZ( *atm );
    coordsD_.push_back( XYZ[0] );
    coordsD_.push_back( XYZ[1] );
    coordsD_.push_back( XYZ[2] );
  }

//  MapCoords(frameIn, ucell, recip, maskIn);
  double e_recip = Recip_ParticleMesh( frameIn.BoxCrd() );

  // TODO branch
  double e_vdw6self, e_vdw6recip;
  if (lw_coeff_ > 0.0) {
    e_vdw6self = Self6();
    e_vdw6recip = LJ_Recip_ParticleMesh( frameIn.BoxCrd() );
    if (debug_ > 0) {
      mprintf("DEBUG: e_vdw6self = %16.8f\n", e_vdw6self);
      mprintf("DEBUG: Evdwrecip = %16.8f\n", e_vdw6recip);
    }
    e_vdw_lr_correction = 0.0;
  } else {
    e_vdw6self = 0.0;
    e_vdw6recip = 0.0;
    e_vdw_lr_correction = Vdw_Correction( volume );
  }

  e_vdw = 0.0;
  double e_direct = Direct( pairList_, e_vdw);
  if (debug_ > 0)
    mprintf("DEBUG: Eself= %20.10f   Erecip= %20.10f   Edirect= %20.10f  Evdw= %20.10f\n",
            e_self, e_recip, e_direct, e_vdw);
  e_vdw += (e_vdw_lr_correction + e_vdw6self + e_vdw6recip);
  t_total_.Stop();
  e_elec = e_self + e_recip + e_direct;
  return 0;
}

/** Calculate full nonbonded energy with PME Used for GIST, adding 6 arrays to store the
 * decomposed energy terms for every atom
 */
int Ewald_ParticleMesh::CalcNonbondEnergy_GIST(Frame const& frameIn, AtomMask const& maskIn,
                                      double& e_elec, double& e_vdw,
                                      std::vector<double>& e_vdw_direct,
                                      std::vector<double>& e_vdw_self,
                                      std::vector<double>& e_vdw_recip,
                                      std::vector<double>& e_vdw_lr_cor,
                                      std::vector<double>& e_elec_self,
                                      std::vector<double>& e_elec_direct,
                                      std::vector<double>& e_elec_recip,
                                      std::vector<int>& atom_voxel )
{
  t_total_.Start();
  Matrix_3x3 ucell, recip;
  double volume = frameIn.BoxCrd().ToRecip(ucell, recip);

  auto step0 = std::chrono::system_clock::now();


  double e_self = Self_GIST( volume, e_elec_self ); // decomposed E_elec_self for GIST

  auto step1 = std::chrono::system_clock::now();

  std::chrono::duration<double> d1 = step1 -step0;

  //mprintf("Eelec_self takes: %f seconds\n", d1.count());


  double e_vdw_lr_correction;

  int retVal = pairList_.CreatePairList(frameIn, ucell, recip, maskIn);
  if (retVal != 0) {
    mprinterr("Error: Grid setup failed.\n");
    return 1;
  }

  // TODO make more efficient
  int idx = 0;
  coordsD_.clear();
  

  for (AtomMask::const_iterator atm = maskIn.begin(); atm != maskIn.end(); ++atm, ++idx) {
    const double* XYZ = frameIn.XYZ( *atm );
    coordsD_.push_back( XYZ[0] );
    coordsD_.push_back( XYZ[1] );
    coordsD_.push_back( XYZ[2] );
    
  }

  const int atom_num = coordsD_.size()/3;

  //mprintf("atom_num is %i \n", atom_num);

 //Potential for each atom
 //helpme::Matrix<double> e_potentialD[atom_num]={0};
  helpme::Matrix<double> e_potentialD(atom_num,4);

  e_potentialD.setConstant(0.0);
  //mprintf("The vaule at row 10,col 1: %f \n", e_potentialD(10,1));

  //Mat chargesD(&Charge_[0], Charge_.size(), 1);

  //Mat e_potentialD = chargesD.clone()
  

//  MapCoords(frameIn, ucell, recip, maskIn);
  double e_recip = Recip_ParticleMesh_GIST( frameIn.BoxCrd(), e_potentialD );

  auto step2 = std::chrono::system_clock::now();


  //mprintf("e_recip finished, the e_recip value: %f \n", e_recip);

  std::chrono::duration<double> d2 = step2 - step1;

  //mprintf("Eelec_recip takes: %f seconds \n", d2.count());


  //Darray* iterator it;
  //int atom_num =0;

  //idx=0;

  /**
  
  for ( Ewald::Darray *it=Charge_; ++it,++idx)
  {
    e_elec_recip.push_back(*it * e_potentialD(idx,1));
    //atom_num=atom_num+1;

  }
  **/

  for(int i =0; i < atom_num;i++)
  {
    e_elec_recip[i]=0.5 * Charge_[i] * e_potentialD(i,0);
  }

  auto step3= std::chrono::system_clock::now();

  std::chrono::duration<double> d3 = step3 -step2;
  //mprintf("decompose Eelec_recip takes: %f seconds \n", d3.count());


  

  // vdw potential for each atom 
  helpme::Matrix<double> vdw_potentialD(atom_num,4);


  // TODO branch
  double e_vdw6self, e_vdw6recip;
  if (lw_coeff_ > 0.0) {

    e_vdw6self = Self6_GIST(e_vdw_self);

    e_vdw6recip = LJ_Recip_ParticleMesh_GIST( frameIn.BoxCrd(),vdw_potentialD );

    mprintf(" e_vdw6self: %f, e_vdw6recip: %f \n", e_vdw6self, e_vdw6recip);

    for(int j=0; j< atom_num;j++){

    e_vdw_recip[j]=0.5 * (Cparam_[j] * vdw_potentialD(j,0)); // Split the energy by half

     // not sure vdw_recip for each atom can be calculated like this?
 
    } // by default, this block of code will not be excuted since the default lw_coeff_=-1


    if (debug_ > 0) {
      mprintf("DEBUG: e_vdw6self = %16.8f\n", e_vdw6self);
      mprintf("DEBUG: Evdwrecip = %16.8f\n", e_vdw6recip);
    }
    e_vdw_lr_correction = 0.0;
  } else {
    e_vdw6self = 0.0;
    e_vdw6recip = 0.0;
    e_vdw_lr_correction = Vdw_Correction_GIST( volume, e_vdw_lr_cor );
    //mprintf("e_vdw_lr_correction: %f \n", e_vdw_lr_correction);
  }

  auto step4= std::chrono::system_clock::now();

  //mprintf("vdw long range correction takes: %f seconds \n", (step4-step3).count());

  e_vdw = 0.0;
  double e_direct = Direct_GIST( pairList_, e_vdw, e_vdw_direct,e_elec_direct, atom_voxel );

  auto step5=std::chrono::system_clock::now();

  std::chrono::duration<double> d4 = step5 -step4;

  //s: %f seconds\n",d4.count());


  //mprintf("e_elec_self: %f , e_elec_direct: %f, e_vdw6direct: %f \n", e_self, e_direct, e_vdw);

  if (debug_ > 0)
    mprintf("DEBUG: Eself= %20.10f   Erecip= %20.10f   Edirect= %20.10f  Evdw= %20.10f\n",
            e_self, e_recip, e_direct, e_vdw);
  e_vdw += (e_vdw_lr_correction + e_vdw6self + e_vdw6recip);
  t_total_.Stop();
  e_elec = e_self + e_recip + e_direct;
  return 0;
}

#endif /* LIBPME */
