// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef PARQUET_COLUMN_STATISTICS_H
#define PARQUET_COLUMN_STATISTICS_H

#include <cstdint>
#include <memory>
#include <string>

#include "parquet/schema/descriptor.h"
#include "parquet/types.h"
#include "parquet/util/buffer.h"
#include "parquet/util/mem-allocator.h"
#include "parquet/util/visibility.h"

namespace parquet {

class PARQUET_EXPORT EncodedStatistics {
  std::shared_ptr<std::string> max_, min_;

 public:
  EncodedStatistics()
      : max_(std::make_shared<std::string>()), min_(std::make_shared<std::string>()) {}

  const std::string& max() const { return *max_; }
  const std::string& min() const { return *min_; }

  int64_t null_count = 0;
  int64_t distinct_count = 0;

  bool has_min = false;
  bool has_max = false;
  bool has_null_count = false;
  bool has_distinct_count = false;

  inline bool is_set() const {
    return has_min || has_max || has_null_count || has_distinct_count;
  }

  inline EncodedStatistics& set_max(const std::string& value) {
    *max_ = value;
    has_max = true;
    return *this;
  }

  inline EncodedStatistics& set_min(const std::string& value) {
    *min_ = value;
    has_min = true;
    return *this;
  }

  inline EncodedStatistics& set_null_count(int64_t value) {
    null_count = value;
    has_null_count = true;
    return *this;
  }

  inline EncodedStatistics& set_distinct_count(int64_t value) {
    distinct_count = value;
    has_distinct_count = true;
    return *this;
  }
};

template <typename DType>
class PARQUET_EXPORT TypedRowGroupStatistics;

class PARQUET_EXPORT RowGroupStatistics
    : public std::enable_shared_from_this<RowGroupStatistics> {
 public:
  int64_t null_count() const { return statistics_.null_count; }
  int64_t distinct_count() const { return statistics_.distinct_count; }
  int64_t num_values() const { return num_values_; }

  virtual bool HasMinMax() const = 0;
  virtual void Reset() = 0;

  // Plain-encoded minimum value
  virtual std::string EncodeMin() = 0;

  // Plain-encoded maximum value
  virtual std::string EncodeMax() = 0;

  virtual EncodedStatistics Encode() = 0;

  virtual ~RowGroupStatistics() {}

  Type::type physical_type() const { return descr_->physical_type(); }

 protected:
  const ColumnDescriptor* descr() const { return descr_; }
  void SetDescr(const ColumnDescriptor* schema) { descr_ = schema; }

  void IncrementNullCount(int64_t n) { statistics_.null_count += n; }

  void IncrementNumValues(int64_t n) { num_values_ += n; }

  void IncrementDistinctCount(int64_t n) { statistics_.distinct_count += n; }

  void MergeCounts(const RowGroupStatistics& other) {
    this->statistics_.null_count += other.statistics_.null_count;
    this->statistics_.distinct_count += other.statistics_.distinct_count;
    this->num_values_ += other.num_values_;
  }

  void ResetCounts() {
    this->statistics_.null_count = 0;
    this->statistics_.distinct_count = 0;
    this->num_values_ = 0;
  }

  const ColumnDescriptor* descr_ = nullptr;
  int64_t num_values_ = 0;
  EncodedStatistics statistics_;
};

template <typename DType>
class TypedRowGroupStatistics : public RowGroupStatistics {
 public:
  using T = typename DType::c_type;

  TypedRowGroupStatistics(
      const ColumnDescriptor* schema, MemoryAllocator* allocator = default_allocator());

  TypedRowGroupStatistics(const T& min, const T& max, int64_t num_values,
      int64_t null_count, int64_t distinct_count);

  TypedRowGroupStatistics(const ColumnDescriptor* schema, const std::string& encoded_min,
      const std::string& encoded_max, int64_t num_values, int64_t null_count,
      int64_t distinct_count, bool has_min_max,
      MemoryAllocator* allocator = default_allocator());

  bool HasMinMax() const override;
  void Reset() override;
  void Merge(const TypedRowGroupStatistics<DType>& other);

  void Update(const T* values, int64_t num_not_null, int64_t num_null);

  const T& min() const { return min_; }
  const T& max() const { return max_; }

  std::string EncodeMin() override;
  std::string EncodeMax() override;
  EncodedStatistics Encode() override;

 private:
  bool has_min_max_ = false;
  T min_;
  T max_;
  MemoryAllocator* allocator_;

  void PlainEncode(const T& src, std::string* dst);
  void PlainDecode(const std::string& src, T* dst);
  void Copy(const T& src, T* dst, OwnedMutableBuffer& buffer);

  OwnedMutableBuffer min_buffer_, max_buffer_;
};

template <typename DType>
inline void TypedRowGroupStatistics<DType>::Copy(
    const T& src, T* dst, OwnedMutableBuffer&) {
  *dst = src;
}

template <>
inline void TypedRowGroupStatistics<FLBAType>::Copy(
    const FLBA& src, FLBA* dst, OwnedMutableBuffer& buffer) {
  if (dst->ptr == src.ptr) return;
  uint32_t len = descr_->type_length();
  buffer.Resize(len);
  std::memcpy(&buffer[0], src.ptr, len);
  *dst = FLBA(buffer.data());
}

template <>
inline void TypedRowGroupStatistics<ByteArrayType>::Copy(
    const ByteArray& src, ByteArray* dst, OwnedMutableBuffer& buffer) {
  if (dst->ptr == src.ptr) return;
  buffer.Resize(src.len);
  std::memcpy(&buffer[0], src.ptr, src.len);
  *dst = ByteArray(src.len, buffer.data());
}

template <>
void TypedRowGroupStatistics<ByteArrayType>::PlainEncode(const T& src, std::string* dst);

template <>
void TypedRowGroupStatistics<ByteArrayType>::PlainDecode(const std::string& src, T* dst);

using BoolStatistics = TypedRowGroupStatistics<BooleanType>;
using Int32Statistics = TypedRowGroupStatistics<Int32Type>;
using Int64Statistics = TypedRowGroupStatistics<Int64Type>;
using Int96Statistics = TypedRowGroupStatistics<Int96Type>;
using FloatStatistics = TypedRowGroupStatistics<FloatType>;
using DoubleStatistics = TypedRowGroupStatistics<DoubleType>;
using ByteArrayStatistics = TypedRowGroupStatistics<ByteArrayType>;
using FLBAStatistics = TypedRowGroupStatistics<FLBAType>;

extern template class TypedRowGroupStatistics<BooleanType>;
extern template class TypedRowGroupStatistics<Int32Type>;
extern template class TypedRowGroupStatistics<Int64Type>;
extern template class TypedRowGroupStatistics<Int96Type>;
extern template class TypedRowGroupStatistics<FloatType>;
extern template class TypedRowGroupStatistics<DoubleType>;
extern template class TypedRowGroupStatistics<ByteArrayType>;
extern template class TypedRowGroupStatistics<FLBAType>;

}  // namespace parquet

#endif  // PARQUET_COLUMN_STATISTICS_H
