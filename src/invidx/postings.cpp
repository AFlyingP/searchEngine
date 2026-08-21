#include "invidx/postings.hpp"

#include <cstring>
#include <stdexcept>

namespace needlefish {

void PostingListWriter::add_posting(uint32_t doc_id, uint32_t term_freq,
                                    std::span<const uint32_t> positions) {
    if (!block_doc_ids_.empty() && doc_id <= block_doc_ids_.back()) {
        throw std::invalid_argument("PostingListWriter: doc_ids must be strictly monotonically increasing");
    }
    block_doc_ids_.push_back(doc_id);
    block_freqs_.push_back(term_freq);
    block_positions_.emplace_back(positions.begin(), positions.end());
    total_docs_++;
    max_doc_id_ = doc_id;
}

void PostingListWriter::flush_batch(size_t start_idx, size_t count,
                                    std::vector<uint8_t>& postings_out,
                                    std::vector<uint8_t>& positions_out, float block_max_score) {
    if (count == 0) {
        return;
    }

    const uint16_t num_docs = static_cast<uint16_t>(count);

    // 1. Calculate docID deltas
    uint32_t deltas[128] = {0};
    uint32_t prev_doc = last_doc_id_in_list_;
    for (size_t i = 0; i < num_docs; ++i) {
        const uint32_t doc = block_doc_ids_[start_idx + i];
        deltas[i] = doc - prev_doc;
        prev_doc = doc;
    }
    last_doc_id_in_list_ = prev_doc;

    // Pad remaining entries to 128
    for (size_t i = num_docs; i < 128; ++i) {
        deltas[i] = 0;
    }

    const uint8_t bit_width = BitPacking::required_bits(deltas);

    // 2. Encode positions stream
    const uint32_t pos_start_offset = static_cast<uint32_t>(positions_out.size());
    std::vector<uint32_t> pos_lengths(num_docs);

    for (size_t i = 0; i < num_docs; ++i) {
        const auto& pos_list = block_positions_[start_idx + i];
        pos_lengths[i] = static_cast<uint32_t>(pos_list.size());
        if (!pos_list.empty()) {
            // Delta-encode positions
            uint32_t prev_p = 0;
            for (uint32_t p : pos_list) {
                uint32_t p_delta = p - prev_p;
                prev_p = p;
                uint8_t vbuf[5];
                const size_t vlen = Varint::encode_uint32(p_delta, vbuf);
                positions_out.insert(positions_out.end(), vbuf, vbuf + vlen);
            }
        }
    }
    const uint32_t pos_bytes = static_cast<uint32_t>(positions_out.size() - pos_start_offset);

    // 3. Write block header
    PostingBlockHeader header{.max_doc_id = block_doc_ids_[start_idx + num_docs - 1],
                              .block_max_score = block_max_score,
                              .num_docs = num_docs,
                              .bit_width = bit_width,
                              .positions_offset = pos_start_offset,
                              .positions_bytes = pos_bytes};

    const size_t header_offset = postings_out.size();
    postings_out.resize(header_offset + sizeof(header));
    std::memcpy(postings_out.data() + header_offset, &header, sizeof(header));

    // 4. Write packed docID deltas
    const size_t packed_bytes = 16 * bit_width;
    const size_t packed_offset = postings_out.size();
    postings_out.resize(packed_offset + packed_bytes);
    BitPacking::pack128(deltas, postings_out.data() + packed_offset, bit_width);

    // 5. Write Varint term frequencies
    for (size_t i = 0; i < num_docs; ++i) {
        uint8_t vbuf[5];
        const size_t vlen = Varint::encode_uint32(block_freqs_[start_idx + i], vbuf);
        postings_out.insert(postings_out.end(), vbuf, vbuf + vlen);
    }

    // 6. Write Varint position lengths
    for (size_t i = 0; i < num_docs; ++i) {
        uint8_t vbuf[5];
        const size_t vlen = Varint::encode_uint32(pos_lengths[i], vbuf);
        postings_out.insert(postings_out.end(), vbuf, vbuf + vlen);
    }
}

void PostingListWriter::finish(std::vector<uint8_t>& postings_out,
                               std::vector<uint8_t>& positions_out,
                               std::span<const float> block_max_scores) {
    size_t processed = 0;
    size_t block_idx = 0;
    while (processed < block_doc_ids_.size()) {
        const size_t batch_size = std::min<size_t>(128, block_doc_ids_.size() - processed);
        float b_max = (block_idx < block_max_scores.size()) ? block_max_scores[block_idx] : 0.0f;
        flush_batch(processed, batch_size, postings_out, positions_out, b_max);
        processed += batch_size;
        block_idx++;
    }
    block_doc_ids_.clear();
    block_freqs_.clear();
    block_positions_.clear();
}

PostingListReader::PostingListReader(std::span<const uint8_t> postings_data,
                                     std::span<const uint8_t> positions_data, size_t total_docs)
    : postings_data_(postings_data)
    , positions_data_(positions_data)
    , total_docs_(total_docs)
    , at_end_(total_docs == 0 || postings_data.empty()) {
    if (!at_end_) {
        load_next_block();
    }
}

void PostingListReader::load_next_block() {
    if (postings_offset_ + sizeof(curr_block_header_) > postings_data_.size() ||
        docs_read_total_ >= total_docs_) {
        at_end_ = true;
        return;
    }

    // Read header
    std::memcpy(&curr_block_header_, postings_data_.data() + postings_offset_,
                sizeof(curr_block_header_));
    postings_offset_ += sizeof(curr_block_header_);

    if (curr_block_header_.num_docs == 0 || curr_block_header_.num_docs > 128 ||
        curr_block_header_.bit_width > 32) {
        at_end_ = true;
        return;
    }

    const size_t packed_bytes = 16 * curr_block_header_.bit_width;
    if (postings_offset_ + packed_bytes > postings_data_.size()) {
        at_end_ = true;
        return;
    }

    // Unpack docID deltas
    uint32_t deltas[128] = {0};
    BitPacking::unpack128(postings_data_.data() + postings_offset_, deltas,
                          curr_block_header_.bit_width);
    postings_offset_ += packed_bytes;

    // Apply prefix sums to deltas
    uint32_t running_doc = curr_doc_id_;
    for (size_t i = 0; i < curr_block_header_.num_docs; ++i) {
        running_doc += deltas[i];
        decoded_doc_ids_[i] = running_doc;
    }

    // Decode Varint freqs
    const uint8_t* ptr = postings_data_.data() + postings_offset_;
    const uint8_t* end = postings_data_.data() + postings_data_.size();

    for (size_t i = 0; i < curr_block_header_.num_docs; ++i) {
        if (ptr >= end) {
            decoded_freqs_[i] = 1;
            continue;
        }
        size_t b = Varint::decode_uint32(ptr, end, &decoded_freqs_[i]);
        if (b == 0) {
            decoded_freqs_[i] = 1;
            break;
        }
        ptr += b;
    }

    // Decode Varint position counts and record byte offsets
    uint32_t curr_pos_stream_offset = curr_block_header_.positions_offset;
    const uint8_t* pos_end = positions_data_.data() + positions_data_.size();

    for (size_t i = 0; i < curr_block_header_.num_docs; ++i) {
        uint32_t pos_count = 0;
        if (ptr < end) {
            size_t b = Varint::decode_uint32(ptr, end, &pos_count);
            ptr += b;
        }
        decoded_pos_lens_[i] = pos_count;
        decoded_pos_offsets_[i] = curr_pos_stream_offset;

        if (curr_pos_stream_offset < positions_data_.size()) {
            const uint8_t* pos_ptr = positions_data_.data() + curr_pos_stream_offset;
            for (uint32_t p = 0; p < pos_count; ++p) {
                if (pos_ptr >= pos_end) break;
                uint32_t dummy = 0;
                size_t b = Varint::decode_uint32(pos_ptr, pos_end, &dummy);
                if (b == 0) break;
                pos_ptr += b;
            }
            curr_pos_stream_offset = static_cast<uint32_t>(pos_ptr - positions_data_.data());
        }
    }

    postings_offset_ = static_cast<size_t>(ptr - postings_data_.data());

    block_idx_ = 0;
    curr_doc_id_ = decoded_doc_ids_[0];
    curr_freq_ = decoded_freqs_[0];
}

void PostingListReader::next() {
    if (at_end_) {
        return;
    }

    block_idx_++;
    docs_read_total_++;

    if (block_idx_ < curr_block_header_.num_docs) {
        curr_doc_id_ = decoded_doc_ids_[block_idx_];
        curr_freq_ = decoded_freqs_[block_idx_];
    } else {
        load_next_block();
    }
}

void PostingListReader::advance(uint32_t target_doc_id) {
    if (at_end_ || curr_doc_id_ >= target_doc_id) {
        return;
    }

    while (!at_end_) {
        // Check if target is beyond current block
        if (target_doc_id > curr_block_header_.max_doc_id) {
            docs_read_total_ += (curr_block_header_.num_docs - block_idx_);
            curr_doc_id_ = curr_block_header_.max_doc_id;
            load_next_block();
            continue;
        }

        // Inside current block: linear / binary scan within the block
        while (block_idx_ < curr_block_header_.num_docs &&
               decoded_doc_ids_[block_idx_] < target_doc_id) {
            block_idx_++;
            docs_read_total_++;
        }

        if (block_idx_ < curr_block_header_.num_docs) {
            curr_doc_id_ = decoded_doc_ids_[block_idx_];
            curr_freq_ = decoded_freqs_[block_idx_];
            return;
        } else {
            load_next_block();
        }
    }
}

void PostingListReader::read_positions(std::vector<uint32_t>& out) const {
    out.clear();
    if (at_end_ || block_idx_ >= curr_block_header_.num_docs || positions_data_.empty()) {
        return;
    }

    const uint32_t count = decoded_pos_lens_[block_idx_];
    const uint32_t offset = decoded_pos_offsets_[block_idx_];
    if (offset >= positions_data_.size()) {
        return;
    }

    out.reserve(count);
    const uint8_t* ptr = positions_data_.data() + offset;
    const uint8_t* end = positions_data_.data() + positions_data_.size();
    uint32_t running_pos = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (ptr >= end) break;
        uint32_t delta = 0;
        size_t b = Varint::decode_uint32(ptr, end, &delta);
        if (b == 0) break;
        ptr += b;
        running_pos += delta;
        out.push_back(running_pos);
    }
}

}  // namespace needlefish
