// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SPLIT_TABS_SPLIT_TAB_VISUAL_DATA_H_
#define COMPONENTS_SPLIT_TABS_SPLIT_TAB_VISUAL_DATA_H_

namespace split_tabs {

enum class SplitTabLayout {
  // A tab will stretch out vertically so one tab in the split will be next to
  // the other.
  kVertical,
  // A tab will stretch out horizontally so one tab in the split will be on top
  // of the other.
  kHorizontal,
  // A three-pane "T" layout where the start column is split into two stacked
  // panes and the end column remains full height.
  kThreePaneStartStacked,
  // A three-pane "T" layout where the end column is split into two stacked
  // panes and the start column remains full height.
  kThreePaneEndStacked,
  // A four-pane 2x2 grid.
  kFourPaneGrid,
};

// Represents the visual state of a split tab, including its layout type and the
// proportional size of the webcontents.
class SplitTabVisualData {
 public:
  SplitTabVisualData();
  explicit SplitTabVisualData(SplitTabLayout split_layout);
  SplitTabVisualData(SplitTabLayout split_layout, double split_ratio);
  ~SplitTabVisualData();

  SplitTabVisualData(const SplitTabVisualData& other) = default;
  SplitTabVisualData(SplitTabVisualData&& other) = default;

  SplitTabVisualData& operator=(const SplitTabVisualData& other) = default;
  SplitTabVisualData& operator=(SplitTabVisualData&& other) = default;

  void set_split_layout(SplitTabLayout split_layout) {
    split_layout_ = split_layout;
  }

  void set_split_ratio(double split_ratio) { split_ratio_ = split_ratio; }

  SplitTabLayout split_layout() const { return split_layout_; }

  double split_ratio() const { return split_ratio_; }

  // Checks whether two instances are visually equivalent.
  friend bool operator==(const SplitTabVisualData&,
                         const SplitTabVisualData&) = default;

 private:
  SplitTabLayout split_layout_;
  // For vertical-based layouts, ratio of the start column width to the
  // available width.
  double split_ratio_ = 0.5;
};

}  // namespace split_tabs

#endif  // COMPONENTS_SPLIT_TABS_SPLIT_TAB_VISUAL_DATA_H_
