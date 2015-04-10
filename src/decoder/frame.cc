#include "frame.hh"

using namespace std;

template <class FrameHeaderType, class MacroblockType>
Frame<FrameHeaderType, MacroblockType>::Frame( const bool show,
					       const bool continuation,
					       const unsigned int width,
					       const unsigned int height,
					       BoolDecoder & first_partition )
  : show_( show ),
    display_width_( width ),
    display_height_( height ),
    header_( first_partition ),
    continuation_header_( continuation, first_partition )
{}

template <class FrameHeaderType, class MacroblockType>
void Frame<FrameHeaderType, MacroblockType>::reinitialize( const bool show,
							   const bool continuation,
					       	      	   const unsigned int width,
					       	      	   const unsigned int height,
					       	      	   BoolDecoder & first_partition )
{
  if ( width != display_width_ || height != display_height_ ) {
    throw Unsupported( "frames of different dimensions" );
  }
  show_ = show;
  header_ = FrameHeaderType( first_partition );
  continuation_header_.clear();
  if (continuation) {
    continuation_header_.initialize( first_partition );
  }

  Y2_.forall( std::mem_fn( &Y2Block::reinitialize ) );
  Y_.forall( std::mem_fn( &YBlock::reinitialize ) );
  U_.forall( std::mem_fn( &UVBlock::reinitialize ) );
  V_.forall( std::mem_fn( &UVBlock::reinitialize ) );
}

template <class FrameHeaderType, class MacroblockType>
ProbabilityArray< num_segments > Frame<FrameHeaderType, MacroblockType>::calculate_mb_segment_tree_probs( void ) const
{
  /* calculate segment tree probabilities if map is updated by this frame */
  ProbabilityArray< num_segments > mb_segment_tree_probs;

  if ( header_.update_segmentation.initialized()
       and header_.update_segmentation.get().mb_segmentation_map.initialized() ) {
    const auto & seg_map = header_.update_segmentation.get().mb_segmentation_map.get();
    for ( unsigned int i = 0; i < mb_segment_tree_probs.size(); i++ ) {
      mb_segment_tree_probs.at( i ) = seg_map.at( i ).get_or( 255 );
    }
  }

  return mb_segment_tree_probs;
}

template <class FrameHeaderType, class MacroblockType>
void Frame<FrameHeaderType, MacroblockType>::parse_macroblock_headers( BoolDecoder & rest_of_first_partition,
								       const ProbabilityTables & probability_tables )
{
  /* calculate segment tree probabilities if map is updated by this frame */
  const ProbabilityArray< num_segments > mb_segment_tree_probs = calculate_mb_segment_tree_probs();

  /* parse the macroblock headers */
  macroblock_headers_.initialize( macroblock_width_, macroblock_height_,
				  rest_of_first_partition, header_,
				  mb_segment_tree_probs,
				  probability_tables,
				  continuation_header_,
				  Y2_, Y_, U_, V_ );

  /* repoint Y2 above/left pointers to skip missing subblocks */
  relink_y2_blocks();
}

template <class FrameHeaderType, class MacroblockType>
void Frame<FrameHeaderType, MacroblockType>::update_segmentation( SegmentationMap & mutable_segmentation_map )
{
  macroblock_headers_.get().forall( [&] ( MacroblockType & mb ) { mb.update_segmentation( mutable_segmentation_map ); } );
}

bool ContinuationHeader::is_missing( const reference_frame reference_id ) const
{
  switch ( reference_id ) {
  case LAST_FRAME: return missing_last_frame;
  case GOLDEN_FRAME: return missing_golden_frame;
  case ALTREF_FRAME: return missing_alternate_reference_frame;
  default: throw LogicError();
  }
}

template <class FrameHeaderType, class MacroblockType>
void Frame<FrameHeaderType, MacroblockType>::parse_tokens( vector< Chunk > dct_partitions,
							   const ProbabilityTables & probability_tables )
{
  vector< BoolDecoder > dct_partition_decoders;

  for ( const auto & x : dct_partitions ) {
    dct_partition_decoders.emplace_back( x );
  }

  /* prepare token probabilities for a continuation macroblock */
  ProbabilityTables continuation_probability_table = probability_tables;
  if ( continuation_header_.initialized() ) {
    for ( unsigned int i = 0; i < BLOCK_TYPES; i++ ) {
      for ( unsigned int j = 0; j < COEF_BANDS; j++ ) {
	for ( unsigned int k = 0; k < PREV_COEF_CONTEXTS; k++ ) {
	  for ( unsigned int l = 0; l < ENTROPY_NODES; l++ ) {
	    continuation_probability_table.coeff_probs.at( i ).at( j ).at( k ).at( l )
	      = continuation_header_.get().continuation_token_probabilities.at( i ).at( j ).at( k ).at( l );
	  }
	}
      }
    }
  }

  /* parse every macroblock's tokens */
  macroblock_headers_.get().forall_ij( [&]( MacroblockType & macroblock,
					    const unsigned int column __attribute((unused)),
					    const unsigned int row )
				       {
					 const ProbabilityTables & prob_table = macroblock.continuation() ? continuation_probability_table : probability_tables;
					 macroblock.parse_tokens( dct_partition_decoders.at( row % dct_partition_decoders.size() ),
								  prob_table ); } );
}

template <class FrameHeaderType, class MacroblockType>
void Frame<FrameHeaderType, MacroblockType>::loopfilter( const Optional< Segmentation > & segmentation,
							 const Optional< FilterAdjustments > & filter_adjustments,
							 Raster & raster ) const
{
  if ( header_.loop_filter_level ) {
    /* calculate per-segment filter adjustments if
       segmentation is enabled */

    const FilterParameters frame_loopfilter( header_.filter_type,
					     header_.loop_filter_level,
					     header_.sharpness_level );

    SafeArray< FilterParameters, num_segments > segment_loopfilters;

    if ( segmentation.initialized() ) {
      for ( uint8_t i = 0; i < num_segments; i++ ) {
	FilterParameters segment_filter( header_.filter_type,
					 header_.loop_filter_level,
					 header_.sharpness_level );
	segment_filter.filter_level = segmentation.get().segment_filter_adjustments.at( i )
	  + ( segmentation.get().absolute_segment_adjustments
	      ? 0
	      : segment_filter.filter_level );

	segment_loopfilters.at( i ) = segment_filter;
      }
    }

    /* the macroblock needs to know whether the mode- and reference-based
       filter adjustments are enabled */

    macroblock_headers_.get().forall_ij( [&]( const MacroblockType & macroblock,
					      const unsigned int column,
					      const unsigned int row )
					 { macroblock.loopfilter( filter_adjustments,
								  segmentation.initialized()
								  ? segment_loopfilters.at( macroblock.segment_id() )
								  : frame_loopfilter,
								  raster.macroblock( column, row ) ); } );
  }
}

template <class FrameHeaderType, class MacroblockType>
SafeArray<Quantizer, num_segments> Frame<FrameHeaderType, MacroblockType>::calculate_segment_quantizers( const Optional< Segmentation > & segmentation ) const
{
  /* calculate per-segment quantizer adjustments if
     segmentation is enabled */

  SafeArray< Quantizer, num_segments > segment_quantizers;

  if ( segmentation.initialized() ) {
    for ( uint8_t i = 0; i < num_segments; i++ ) {
      QuantIndices segment_indices( header_.quant_indices );
      segment_indices.y_ac_qi = segmentation.get().segment_quantizer_adjustments.at( i )
	+ ( segmentation.get().absolute_segment_adjustments
	    ? static_cast<Unsigned<7>>( 0 )
	    : segment_indices.y_ac_qi );

      segment_quantizers.at( i ) = Quantizer( segment_indices );
    }
  }

  return segment_quantizers;
}

template <>
void KeyFrame::decode( const Optional< Segmentation > & segmentation,
		       const Optional< FilterAdjustments > & filter_adjustments,
		       Raster & raster, const bool lf ) const
{
  const Quantizer frame_quantizer( header_.quant_indices );
  const auto segment_quantizers = calculate_segment_quantizers( segmentation );

  /* process each macroblock */
  macroblock_headers_.get().forall_ij( [&]( const KeyFrameMacroblock & macroblock,
					    const unsigned int column,
					    const unsigned int row ) {
					 const auto & quantizer = segmentation.initialized()
					   ? segment_quantizers.at( macroblock.segment_id() )
					   : frame_quantizer;
					 macroblock.reconstruct_intra( quantizer,
								       raster.macroblock( column, row ) );
				       } );
  if ( lf ) {
    loopfilter( segmentation, filter_adjustments, raster );
  }
}

template <>
void InterFrame::decode( const Optional< Segmentation > & segmentation,
			 const Optional< FilterAdjustments > & filter_adjustments,
			 const References & references,
			 Raster & raster, const bool lf ) const
{
  const Quantizer frame_quantizer( header_.quant_indices );
  const auto segment_quantizers = calculate_segment_quantizers( segmentation );

  /* process each macroblock */
  macroblock_headers_.get().forall_ij( [&]( const InterFrameMacroblock & macroblock,
					    const unsigned int column,
					    const unsigned int row ) { 
					 const auto & quantizer = segmentation.initialized()
					   ? segment_quantizers.at( macroblock.segment_id() )
					   : frame_quantizer;
					 if ( macroblock.inter_coded() ) {
					   if ( macroblock.continuation() ) {
					     macroblock.reconstruct_continuation( references,
										  raster.macroblock( column, row ) );
					   } else {
					     macroblock.reconstruct_inter( quantizer,
									   references,
									   raster.macroblock( column, row ) );
					   }
					 } else {
					   macroblock.reconstruct_intra( quantizer,
									 raster.macroblock( column, row ) );
					 } } );

  if ( lf ) {
    loopfilter( segmentation, filter_adjustments, raster );
  }
}

/* "above" for a Y2 block refers to the first macroblock above that actually has Y2 coded */
/* here we relink the "above" and "left" pointers after we learn the prediction mode
   for the block */
template <class FrameHeaderType, class MacroblockType>
void Frame<FrameHeaderType, MacroblockType>::relink_y2_blocks( void )
{
  vector< Optional< const Y2Block * > > above_coded( macroblock_width_ );
  vector< Optional< const Y2Block * > > left_coded( macroblock_height_ );
  
  Y2_.forall_ij( [&]( Y2Block & block, const unsigned int column, const unsigned int row ) {
      block.set_above( above_coded.at( column ) );
      block.set_left( left_coded.at( row ) );
      if ( block.coded() ) {
	above_coded.at( column ) = &block;
	left_coded.at( row ) = &block;
      }
    } );
}

template <>
void KeyFrame::copy_to( const RasterHandle & raster, References & references ) const
{
  references.last = references.golden = references.alternative_reference = raster;
}

template <>
void InterFrame::copy_to( const RasterHandle & raster, References & references ) const
{
  if ( header_.copy_buffer_to_alternate.initialized() ) {
    if ( header_.copy_buffer_to_alternate.get() == 1 ) {
      references.alternative_reference = references.last;
    } else if ( header_.copy_buffer_to_alternate.get() == 2 ) {
      references.alternative_reference = references.golden;
    }
  }

  if ( header_.copy_buffer_to_golden.initialized() ) {
    if ( header_.copy_buffer_to_golden.get() == 1 ) {
      references.golden = references.last;
    } else if ( header_.copy_buffer_to_golden.get() == 2 ) {
      references.golden = references.alternative_reference;
    }
  }

  if ( header_.refresh_golden_frame ) {
    references.golden = raster;
  }

  if ( header_.refresh_alternate_frame ) {
    references.alternative_reference = raster;
  }

  if ( header_.refresh_last ) {
    references.last = raster;
  }
}

template class Frame< KeyFrameHeader, KeyFrameMacroblock >;
template class Frame< InterFrameHeader, InterFrameMacroblock >;
