#ifndef ALFALFA_VIDEO_HH
#define ALFALFA_VIDEO_HH

/* object that represents a .alf "video directory"
   on disk.

   The components are:

   1. one or more IVF files containing coded frames
   2. a "raster list" of hashes of the original
      rasters that the video approximates
   3. a "quality database" of the relation
      original raster / approximate raster / quality
   4. a "frame database" of the relation
      frame name / filename / offset / length
   5. a "trajectory database" of the relation
      ID => { list of frame names }

*/

#define FRAME_DB_FILENAME "frame.db"
#define RASTER_LIST_FILENAME "raster.db"
#define QUALITY_DB_FILENAME "quality.db"
#define TRACK_DB_FILENAME "track.db"
#define VIDEO_MANIFEST_FILENAME "video.manifest"

#include <unordered_map>
#include <unordered_set>

#include "db.hh"
#include "ivf.hh"
#include "manifests.hh"
#include "frame_db.hh"
#include "ivf_writer.hh"

class AlfalfaVideo
{
public:
  class VideoDirectory
  {
  private:
    std::string directory_path_;

  public:
    VideoDirectory( const std::string & name );

    const std::string & path() const { return directory_path_; }
    std::string video_manifest_filename() const;
    std::string raster_list_filename() const;
    std::string quality_db_filename() const;
    std::string frame_db_filename() const;
    std::string track_db_filename() const;
  };

private:
  VideoDirectory directory_;
  OpenMode mode_;
  VideoManifest video_manifest_;
  RasterList raster_list_;
  QualityDB quality_db_;
  FrameDB frame_db_;
  TrackDB track_db_;
  std::unordered_set<size_t> track_ids_;

public:
  AlfalfaVideo( const std::string & directory_name, OpenMode mode );

  bool can_combine( const AlfalfaVideo & video );
  void combine( const AlfalfaVideo & video, IVFWriter & combined_ivf_writer );
  void import_ivf_file( const std::string & filename );

  VideoManifest & video_manifest() { return video_manifest_; }
  const VideoManifest video_manifest() const { return video_manifest_; }

  RasterList & raster_list() { return raster_list_; }
  const RasterList & raster_list() const { return raster_list_; }

  QualityDB & quality_db() { return quality_db_; }
  const QualityDB & quality_db() const { return quality_db_; }

  FrameDB & frame_db() { return frame_db_; }
  const FrameDB & frame_db() const { return frame_db_; }

  TrackDB & track_db() { return track_db_; }
  const TrackDB & track_db() const { return track_db_; }

  const string get_ivf_file_name() { return "v"; }

  std::pair<std::unordered_set<size_t>::iterator, std::unordered_set<size_t>::iterator> get_track_ids();
  FrameInfo get_first_frame( const size_t & track_id );
  Optional<FrameInfo> get_next_frame_info( const size_t & track_id, const size_t & frame_index );
  // TODO(Deepak): Insert stub for get_switches here
  double get_quality( int raster_index, const FrameInfo & frame_info );

  bool good() const;
  bool save();
};

#endif /* ALFALFA_VIDEO_HH */
