#ifndef INC_ACTION_CLUSTERING_H
#define INC_ACTION_CLUSTERING_H
#include "Action.h"
#include "TriangleMatrix.h"
#include "ClusterList.h"
#include "TrajectoryFile.h"
// Class: Action_Clustering
/// Used to perform clustering of frames, currently by RMSD only.
class Action_Clustering: public Action {
  public:
    Action_Clustering();

    void print();
  private:
    int init();
    int action();

    FrameList ReferenceFrames_; ///< Hold frames from all trajin stmts
    AtomMask Mask0_;            ///< Atoms to cluster on
    double epsilon_;            ///< Once the min distance is > epsilon, stop clustering
    int targetNclusters_;       ///< Once there are targetNclusters, stop clustering
    DataSet* cnumvtime_;        ///< Cluster vs time dataset
    char* summaryfile_;         ///< Summary file name
    char* halffile_;            ///< 1st/2nd half summary file name
    char* clusterfile_;         ///< Cluster trajectory base filename.
    char* singlerepfile_;       ///< Cluster all rep single trajectory filename.
    char* reptrajfile_;         ///< Cluster rep to separate trajectory filename.
    char* clusterinfo_;         ///< Name for Ptraj-like cluster output file.
    bool nofitrms_;             ///< If true do not best-fit when calc RMSD.
    bool grace_color_;          ///< If true print grace colors instead of cluster number
    bool load_pair_;            ///< If true, previously calcd pair dist file will be used if found
    DataSet* cluster_dataset_;  ///< Dataset to use for clustering.
    /// Cluster linkage type
    ClusterList::LINKAGETYPE Linkage_;
    /// Cluster trajectory format.
    TrajectoryFile::TrajFormatType clusterfmt_;
    /// Cluster all rep single trajectory format.
    TrajectoryFile::TrajFormatType singlerepfmt_;
    /// Cluster rep to separate trajectory format.
    TrajectoryFile::TrajFormatType reptrajfmt_;
    static const char PAIRDISTFILE[];

    int calcDistFromRmsd( TriangleMatrix &);
    int ClusterHierAgglo( TriangleMatrix &, ClusterList&);
    void CreateCnumvtime( ClusterList & );
    void WriteClusterTraj( ClusterList & );
    void WriteSingleRepTraj( ClusterList & );
    void WriteRepTraj( ClusterList & );
    void calcDistFromDataset( TriangleMatrix & );
};
#endif
