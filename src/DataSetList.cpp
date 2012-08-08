// DataSetList
#include <cstdio> // sprintf FIXME: Get rid of
#include <cstring>
#include <algorithm> // sort
// This also includes basic DataSet class and dataType
#include "DataSetList.h"
#include "CpptrajStdio.h"
#include "ArgList.h"
// Data types go here
#include "DataSet_double.h"
#include "DataSet_string.h"
#include "DataSet_integer.h"
#include "DataSet_float.h"
#include "Histogram.h"
#include "TriangleMatrix.h"
#include "Matrix_2D.h"

// CONSTRUCTOR
DataSetList::DataSetList() :
  debug_(0),
  hasCopies_(false), 
  maxFrames_(0),
  vecidx_(0) 
{
  //fprintf(stderr,"DSL Constructor\n");
}

// DESTRUCTOR
DataSetList::~DataSetList() {
  //fprintf(stderr,"DSL Destructor\n");
  if (!hasCopies_)
    for (DataListType::iterator ds = DataList_.begin(); ds != DataList_.end(); ++ds) 
      delete *ds; 
}

// DataSetList::begin()
DataSetList::const_iterator DataSetList::begin() const {
  return DataList_.begin();
}

// DataSetList::end()
DataSetList::const_iterator DataSetList::end() const {
  return DataList_.end();
}

// DataSetList::erase()
// NOTE: In order to call erase, must use iterator and not const_iterator.
//       Hence, the conversion. The new standard *should* allow const_iterator
//       to be passed to erase(), but this is currently not portable.
/** Erase element pointed to by posIn from the list. */
void DataSetList::erase( const_iterator posIn ) {
  std::vector<DataSet*>::iterator pos = DataList_.begin() + (posIn - DataList_.begin());  
  DataList_.erase( pos ); 
} 

// DataSetList::sort()
void DataSetList::sort() {
  std::sort( DataList_.begin(), DataList_.end(), dsl_cmp() );
}

// DataSetList::SetDebug()
void DataSetList::SetDebug(int debugIn) {
  debug_ = debugIn;
  if (debug_>0) 
    mprintf("DataSetList Debug Level set to %i\n",debug_);
}

// DataSetList::SetMax()
/** Set the max number frames expected to be read in. Used to preallocate
  * data set sizes in the list.
  */
void DataSetList::SetMax(int expectedMax) {
  maxFrames_ = expectedMax;
  if (maxFrames_<0) maxFrames_=0;
}

/* DataSetList::SetPrecisionOfDatasets()
 * Set the width and precision for all datasets in the list.
 */
void DataSetList::SetPrecisionOfDatasets(int widthIn, int precisionIn) {
  for (DataListType::iterator ds = DataList_.begin(); ds != DataList_.end(); ++ds) 
    (*ds)->SetPrecision(widthIn,precisionIn);
}

// DataSetList::ParseArgString()
/** Separate argument nameIn specifying DataSet into name, index, and 
  * attribute parts.
  * Possible formats:
  *  - "<name>"         : Plain dataset name.
  *  - "<name>:<index>" : Dataset within larger overall set (e.g. perres:1)
  *  - "<name>[<attr>]" : Dataset with name and given attribute (e.g. rog[max])
  *  - "<name>[<attr>]:<index>" : 
  *       Dataset with name, given attribute, and index (e.g. NA[shear]:1)
  */
std::string DataSetList::ParseArgString(std::string const& nameIn, int& idxnum,
                                        std::string& attr_arg)
{
  std::string dsname( nameIn );
  attr_arg.clear();
  idxnum = -1;
  //mprinterr("DBG: ParseArgString called with %s\n", nameIn.c_str());
  // Separate out index if present
  size_t idx_pos = dsname.find( ':' );
  if ( idx_pos != std::string::npos ) {
    // Advance to after the ':'
    std::string idx_arg = dsname.substr( idx_pos + 1 );
    //mprinterr("DBG:\t\tIndex Arg [%s]\n", idx_arg.c_str());
    idxnum = convertToInteger( idx_arg );
    // Allow only positive indices
    if ( idxnum < 0 ) {
      mprinterr("Error: DataSet arg %s, index value must be positive! (%i)\n",
                nameIn.c_str(), idxnum);
      return NULL;
    }
    // Drop the index arg
    dsname.resize( idx_pos );
  }

  // Separate out attribute if present
  size_t attr_pos0 = dsname.find_first_of( '[' );
  size_t attr_pos1 = dsname.find_last_of( ']' );
  if ( attr_pos0 != std::string::npos && attr_pos1 != std::string::npos ) {
    if ( (attr_pos0 != std::string::npos && attr_pos1 == std::string::npos) ||
         (attr_pos0 == std::string::npos && attr_pos1 != std::string::npos) )
    {
      mprinterr("Error: Malformed attribute ([<attr>]) in dataset name %s\n", nameIn.c_str());
      return NULL;
    }
    // Advance to after '[', length is position of ']' minus '[' minus 1 
    attr_arg = dsname.substr( attr_pos0 + 1, attr_pos1 - attr_pos0 - 1 );
    //mprinterr("DBG:\t\tAttr Arg [%s]\n", attr_arg.c_str());
    // Drop the attribute arg
    dsname.resize( attr_pos0 );
  }
  //mprinterr("DBG:\t\tName Arg [%s]\n", dsname.c_str());

  return dsname;
}

// DataSetList::GetMultipleSets()
/** \return a list of all DataSets matching the given argument. */
DataSetList DataSetList::GetMultipleSets( std::string const& nameIn ) {
  DataSetList dsetOut;
  dsetOut.hasCopies_ = true;

  // Create a comma-separated list
  ArgList comma_sep( nameIn, "," );
  for (int iarg = 0; iarg < comma_sep.Nargs(); ++iarg) {
    int idxnum = -1;
    std::string attr_arg;
    std::string dsname = ParseArgString( comma_sep[iarg], idxnum, attr_arg );
    //mprinterr("DBG: GetMultipleSets \"%s\": Looking for %s[%s]:%i\n",nameIn.c_str(), dsname.c_str(), attr_arg.c_str(), idxnum);

    for (DataListType::iterator ds = DataList_.begin(); ds != DataList_.end(); ++ds) {
      if ( (*ds)->Matches( dsname, idxnum, attr_arg ) )
      //if ( (*ds)->Name() == nameIn )
        dsetOut.DataList_.push_back( *ds );
    }
  }

  return dsetOut;
}

// DataSetList::Get()
/** \return dataset in the list indicated by nameIn. 
  */
DataSet *DataSetList::Get(const char* nameIn) {
  std::string attr_arg;
  int idxnum = -1;
  std::string dsname = ParseArgString( nameIn, idxnum, attr_arg );

  return GetSet( dsname, idxnum, attr_arg );
}

// DataSetList::GetSet()
/** \return Specified Dataset or NULL if not found.
  */
DataSet* DataSetList::GetSet(std::string const& dsname, int idx, std::string const& aspect) 
{
  for (DataListType::iterator ds = DataList_.begin(); ds != DataList_.end(); ++ds) 
    if ( (*ds)->Matches( dsname, idx, aspect ) ) return *ds;
  return NULL;
}

// DataSetList::AddSet()
/** Add a DataSet with given name, or if no name given create a name based on 
  * defaultName and DataSet position.
  */
DataSet* DataSetList::AddSet( DataSet::DataType inType, std::string const& nameIn,
                              const char* defaultName )
{
  if (nameIn.empty())
    return Add( inType, NULL, defaultName );
  else
    return Add( inType, nameIn.c_str(), defaultName );
}

// DataSetList::Add()
/** Used to add a DataSet to the DataSetList which may or may not
  * be named. If nameIn is not specified create a name based on the 
  * given defaultName and dataset #.
  */
DataSet* DataSetList::Add(DataSet::DataType inType, const char *nameIn,
                          const char *defaultName)
{
  std::string dsname;
  // Require a default name
  if (defaultName == NULL) {
    mprinterr("Internal Error: DataSetList::Add() called without default name.\n");
    return NULL;
  }
  // If nameIn is NULL, generate a name based on defaultName
  if (nameIn == NULL) {
    // Determine size of name + extension
    size_t namesize = strlen( defaultName );
    size_t extsize = (size_t) DigitWidth( size() ); // # digits
    if (extsize < 5) extsize = 5;                   // Minimum size is 5 digits
    extsize += 2;                                   // + underscore + null
    namesize += extsize;
    char* newName = new char[ namesize ];
    sprintf(newName,"%s_%05i", defaultName, size());
    dsname.assign( newName );
    delete[] newName;
  } else
    dsname.assign( nameIn );

  return AddSet( inType, dsname, -1, std::string(), 0 );
}

// DataSetList::AddSetIdx()
/** Add DataSet of specified type with given name and index to list. */
DataSet* DataSetList::AddSetIdx(DataSet::DataType inType,
                                std::string const& nameIn, int idxIn)
{
  return AddSet( inType, nameIn, idxIn, std::string(), 0 );
}

// DataSetList::AddSetAspect()
/** Add DataSet of specified type with given name and aspect to list. */
DataSet* DataSetList::AddSetAspect(DataSet::DataType inType,
                                   std::string const& nameIn,
                                   std::string const& aspectIn)
{
  return AddSet( inType, nameIn, -1, aspectIn, 0 );
}

// DataSetList::AddSetIdxAspect()
DataSet* DataSetList::AddSetIdxAspect(DataSet::DataType inType,
                                      std::string const& nameIn,
                                      int idxIn, std::string const& aspectIn)
{
  return AddSet( inType, nameIn, idxIn, aspectIn, 0 );
}

// DataSetList::AddSetIdxAspect()
DataSet* DataSetList::AddSetIdxAspect(DataSet::DataType inType,
                                      std::string const& nameIn,
                                      int idxIn, std::string const& aspectIn,
                                      std::string const& legendIn)
{
  DataSet* ds = AddSet( inType, nameIn, idxIn, aspectIn, 0 );
  if (ds != NULL)
    ds->SetLegend( legendIn );
  return ds;
}

// DataSetList::AddSet()
/** Add a DataSet of specified type, set it up and return pointer to it. 
  * \param inType type of DataSet to add.
  * \param nameIn DataSet name.
  * \param idxIn DataSet index, -1 if not specified.
  * \param aspectIn DataSet aspect, empty if not specified.
  * \param MAXin Size to set dataset to; DataSet will be set to maxFrames if < 1.
  * \return pointer to successfully set-up dataset.
  */ 
DataSet* DataSetList::AddSet(DataSet::DataType inType, 
                             std::string const& nameIn, int idxIn,
                             std::string const& aspectIn, int MAXin) 
{
  int err = 0;

  // Do not add to a list with copies
  if (hasCopies_) {
    mprinterr("Internal Error: Adding DataSet %s copy to invalid list.\n", nameIn.c_str());
    return NULL;
  }

  // Check if DataSet with same attributes already present.
  DataSet* DS = GetSet(nameIn, idxIn, aspectIn);
  if (DS != NULL) {
    mprintf("Warning: DataSet %s:%i already present.\n", nameIn.c_str(), idxIn);
    // NOTE: Should return found dataset?
    return NULL; 
  }

  switch (inType) {
    case DataSet::DOUBLE       : DS = new DataSet_double(); break;
    case DataSet::FLOAT        : DS = new DataSet_float(); break;
    case DataSet::STRING       : DS = new DataSet_string(); break;
    case DataSet::INT          : DS = new DataSet_integer(); break;
    case DataSet::HIST         : DS = new Histogram(); break;
    case DataSet::TRIMATRIX    : DS = new TriangleMatrix(); break;
    case DataSet::MATRIX2D     : DS = new Matrix_2D(); break;
    case DataSet::UNKNOWN_DATA :
    default:
      mprinterr("Error: DataSetList::Add: Unknown set type.\n");
      return NULL;
  }
  if (DS==NULL) {
    mprinterr("Internal Error: DataSet %s memory allocation failed.\n", nameIn.c_str());
    return NULL;
  }

  // Set up dataset with specified or default (maxFrames_) max.
  if ( MAXin < 1 )
    err = DS->SetupSet(nameIn, maxFrames_, idxIn, aspectIn);
  else
    err = DS->SetupSet(nameIn, MAXin, idxIn, aspectIn);
  if ( err != 0 ) {
    mprinterr("Error setting up data set %s.\n",nameIn.c_str());
    delete DS;
    return NULL;
  }

  DataList_.push_back(DS); 
  //fprintf(stderr,"ADDED dataset %s\n",dsetName);
  return DS;
}

// DataSetList::AddCopyOfSet()
void DataSetList::AddCopyOfSet(DataSet* dsetIn) {
  if (!hasCopies_ && !DataList_.empty()) {
    mprinterr("Internal Error: Adding DataSet (%s) copy to invalid list\n", dsetIn->c_str());
    return;
  }
  hasCopies_ = true;
  DataList_.push_back( dsetIn );
}

// DataSetList::AddDataSet()
int DataSetList::AddDataSet(DataSet* dsetIn) {
  if (dsetIn==NULL) return 1;
  DataList_.push_back(dsetIn);
  return 0;
}

// DataSetList::Info()
/** Print information on all data sets in the list, as well as any datafiles
  * that will be written to.
  */
void DataSetList::Info() {
  if (DataList_.empty())
    mprintf("  There are no data sets set up for analysis.");
  else if (DataList_.size()==1)
    mprintf("  There is 1 data set: ");
  else
    mprintf("  There are %zu data sets: ", DataList_.size());

  mprintf("\n");
  for (unsigned int ds=0; ds<DataList_.size(); ds++) {
    //if (ds>0) mprintf(",");
    //mprintf("%s",DataList_[ds]->c_str());
    //mprintf("%s",DataList_[ds]->Legend().c_str());
    DataList_[ds]->Info();
  }
  //mprintf("\n");
}

// DataSetList::Sync()
void DataSetList::Sync() {
  // Sync datasets - does nothing if worldsize is 1
  for (DataListType::iterator ds = DataList_.begin(); ds != DataList_.end(); ++ds) {
    if ( (*ds)->Sync() ) {
      rprintf( "Error syncing dataset %s\n",(*ds)->c_str());
      //return;
    }
  }
}

// ---------- VECTOR ROUTINES
void DataSetList::VectorBegin() {
  vecidx_ = 0;
}

DataSet* DataSetList::NextVector() {
  for (int idx = vecidx_; idx < (int)DataList_.size(); ++idx) {
    if (DataList_[idx]->Type() == DataSet::VECTOR) {
      // Position vecidx at the next dataset
      vecidx_ = idx + 1;
      return DataList_[idx];
    }
  }
  return 0;
}

DataSet* DataSetList::NextMatrix() {
  for (int idx = vecidx_; idx < (int)DataList_.size(); ++idx) {
    if (DataList_[idx]->Type() == DataSet::MATRIX) {
      // Position vecidx at the next dataset
      vecidx_ = idx + 1;
      return DataList_[idx];
    }
  }
  return 0;
}

DataSet* DataSetList::NextModes() {
  for (int idx = vecidx_; idx < (int)DataList_.size(); ++idx) {
    if (DataList_[idx]->Type() == DataSet::MODES) {
      // Position vecidx at the next dataset
      vecidx_ = idx + 1;
      return DataList_[idx];
    }
  }
  return 0;
}

