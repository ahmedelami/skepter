// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/multi_contents_view.h"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "base/check_deref.h"
#include "base/feature_list.h"
#include "base/i18n/rtl.h"
#include "base/notreached.h"
#include "chrome/browser/actor/ui/actor_overlay_web_view.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_rounded_corner.h"
#include "chrome/browser/ui/views/frame/contents_separator.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/custom_floating_corner.h"
#include "chrome/browser/ui/views/frame/multi_contents_background_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_drop_target_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_resize_area.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_delegate.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_drop_target_controller.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_mini_toolbar.h"
#include "chrome/browser/ui/views/frame/scrim_view.h"
#include "chrome/browser/ui/views/frame/top_container_background.h"
#include "chrome/browser/ui/views/new_tab_footer/footer_web_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/vector_icons/vector_icons.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ozone_buildflags.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_type.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_class_properties.h"

namespace {
constexpr int kSnapDistance = 15;
constexpr int kDragSwapIndicatorSize = 16;

class DragSwapIndicatorImageSource : public gfx::CanvasImageSource {
 public:
  explicit DragSwapIndicatorImageSource(SkColor color)
      : gfx::CanvasImageSource(
            gfx::Size(kDragSwapIndicatorSize, kDragSwapIndicatorSize)),
        color_(color) {}

  DragSwapIndicatorImageSource(const DragSwapIndicatorImageSource&) = delete;
  DragSwapIndicatorImageSource& operator=(const DragSwapIndicatorImageSource&) =
      delete;
  ~DragSwapIndicatorImageSource() override = default;

  void Draw(gfx::Canvas* canvas) override {
    // Two opposing arrows, stacked, to communicate "swap".
    constexpr int kIconSize = 12;
    constexpr int kX = (kDragSwapIndicatorSize - kIconSize) / 2;

    canvas->Save();
    canvas->Translate(gfx::Vector2d(kX, /*y=*/0));
    gfx::PaintVectorIcon(canvas, vector_icons::kArrowRightAltIcon, kIconSize,
                         color_);
    canvas->Restore();

    canvas->Save();
    canvas->Translate(gfx::Vector2d(kX, /*y=*/4));
    gfx::PaintVectorIcon(canvas, vector_icons::kArrowBackIcon, kIconSize,
                         color_);
    canvas->Restore();
  }

 private:
  const SkColor color_;
};

gfx::ImageSkia CreateDragSwapIndicatorImage(
    ui::ColorId color_id,
    const ui::ColorProvider* color_provider) {
  CHECK(color_provider);
  return gfx::ImageSkia(
      std::make_unique<DragSwapIndicatorImageSource>(
          color_provider->GetColor(color_id)),
      gfx::Size(kDragSwapIndicatorSize, kDragSwapIndicatorSize));
}
}

void MultiContentsView::ContentsSeparators::Reset() {
  top_separator = nullptr;
  leading_separator = nullptr;
  trailing_separator = nullptr;
  corner_separator = nullptr;
}

MultiContentsView::MultiContentsView(
    BrowserView* browser_view,
    std::unique_ptr<MultiContentsViewDelegate> delegate)
    : browser_view_(browser_view),
      delegate_(std::move(delegate)),
      start_contents_view_inset_(
          gfx::Insets(kSplitViewContentInset).set_top(0).set_right(0)),
      end_contents_view_inset_(
          gfx::Insets(kSplitViewContentInset).set_top(0).set_left(0)) {
  SetLayoutManager(std::make_unique<views::DelegatingLayoutManager>(this));
  SetProperty(views::kElementIdentifierKey, kMultiContentsViewElementId);

  background_view_ =
      AddChildView(std::make_unique<MultiContentsBackgroundView>(browser_view));

  contents_container_views_.push_back(
      AddChildView(std::make_unique<ContentsContainerView>(browser_view_)));
  contents_container_views_[0]
      ->contents_view()
      ->set_is_primary_web_contents_for_window(true);

  resize_area_ = AddChildView(std::make_unique<MultiContentsResizeArea>(this));
  resize_area_->SetVisible(false);

  drag_swap_indicator_ =
      AddChildView(std::make_unique<views::ImageView>());
  drag_swap_indicator_->SetVisible(false);
  drag_swap_indicator_->SetCanProcessEventsWithinSubtree(false);
  drag_swap_indicator_->SetPaintToLayer(ui::LAYER_TEXTURED);
  drag_swap_indicator_->layer()->SetFillsBoundsOpaquely(false);
  drag_swap_indicator_->SetImage(ui::ImageModel::FromImageGenerator(
      base::BindRepeating(&CreateDragSwapIndicatorImage,
                          kColorMultiContentsViewHighlightContentOutline),
      gfx::Size(kDragSwapIndicatorSize, kDragSwapIndicatorSize)));

  contents_container_views_.push_back(
      AddChildView(std::make_unique<ContentsContainerView>(browser_view_)));
  contents_container_views_[1]->SetVisible(false);

  contents_container_views_.push_back(
      AddChildView(std::make_unique<ContentsContainerView>(browser_view_)));
  contents_container_views_[2]->SetVisible(false);

  contents_container_views_.push_back(
      AddChildView(std::make_unique<ContentsContainerView>(browser_view_)));
  contents_container_views_[3]->SetVisible(false);

  drop_target_view_ =
      AddChildView(std::make_unique<MultiContentsDropTargetView>());
  drop_target_controller_ =
      std::make_unique<MultiContentsViewDropTargetController>(
          *drop_target_view_, *delegate_, g_browser_process->local_state());

  contents_separators_.top_separator =
      AddChildView(ContentsSeparator::CreateLayerBasedContentsSeparator());
  contents_separators_.top_separator->SetProperty(
      views::kElementIdentifierKey, kContentsSeparatorTopEdgeElementId);

  contents_separators_.leading_separator =
      AddChildView(ContentsSeparator::CreateLayerBasedContentsSeparator());
  contents_separators_.leading_separator->SetProperty(
      views::kElementIdentifierKey, kContentsSeparatorLeadingEdgeElementId);

  contents_separators_.trailing_separator =
      AddChildView(ContentsSeparator::CreateLayerBasedContentsSeparator());
  contents_separators_.trailing_separator->SetProperty(
      views::kElementIdentifierKey, kContentsSeparatorTrailingEdgeElementId);

  contents_separators_.corner_separator =
      AddChildView(std::make_unique<CustomFloatingCorner>(
          *browser_view_, CustomFloatingCorner::CornerOrientation::kTopLeading,
          views::ShapeContextTokens::kContentSeparatorRadius,
          CustomFloatingCorner::TopContainerTheme(),
          kColorToolbarContentAreaSeparator));
  contents_separators_.corner_separator->SetProperty(
      views::kElementIdentifierKey, kContentsSeparatorTopCornerElementId);

  for (auto* contents_container_view : contents_container_views_) {
    web_contents_focused_subscriptions_.push_back(
        contents_container_view->contents_view()->AddWebContentsFocusedCallback(
            base::BindRepeating(&MultiContentsView::OnWebContentsFocused,
                                base::Unretained(this))));

    if (contents_container_view->new_tab_footer_view()) {
      ntp_footer_focused_subscriptions_.push_back(
          contents_container_view->new_tab_footer_view()
              ->AddWebContentsFocusedCallback(
                  base::BindRepeating(&MultiContentsView::OnNtpFooterFocused,
                                      base::Unretained(this))));
    }

    if (contents_container_view->actor_overlay_web_view()) {
      actor_overlay_focused_subscriptions_.push_back(
          contents_container_view->actor_overlay_web_view()
              ->AddWebContentsFocusedCallback(
                  base::BindRepeating(&MultiContentsView::OnActorOverlayFocused,
                                      base::Unretained(this))));
    }
  }

  is_drag_drop_pref_enabled_ =
      browser_view_->GetProfile()->GetPrefs()->GetBoolean(
          prefs::kSplitViewDragAndDropEnabled);

  pref_change_registrar_.Init(browser_view_->GetProfile()->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kSplitViewDragAndDropEnabled,
      base::BindRepeating(&MultiContentsView::OnDragAndDropPrefStateChange,
                          base::Unretained(this)));
}

MultiContentsView::~MultiContentsView() {
  if (drop_target_controller_) {
    drop_target_controller_.reset();
  }
  drop_target_view_ = nullptr;
  resize_area_ = nullptr;
  drag_swap_indicator_ = nullptr;
  contents_separators_.Reset();
  background_view_ = nullptr;
  RemoveAllChildViews();
}

ContentsWebView* MultiContentsView::GetActiveContentsView() const {
  return GetActiveContentsContainerView()->contents_view();
}

ContentsWebView* MultiContentsView::GetInactiveContentsView() const {
  return GetInactiveContentsContainerView()->contents_view();
}

ContentsContainerView* MultiContentsView::GetActiveContentsContainerView()
    const {
  return contents_container_views_[active_index_];
}

ContentsContainerView* MultiContentsView::GetInactiveContentsContainerView()
    const {
  return contents_container_views_[GetInactiveIndex()];
}

const gfx::RoundedCornersF& MultiContentsView::background_radii() const {
  return background_view_->GetRoundedCorners();
}

void MultiContentsView::SetBackgroundRadii(const gfx::RoundedCornersF& radii) {
  background_view_->SetRoundedCorners(radii);
}

ContentsContainerView* MultiContentsView::GetContentsContainerViewFor(
    content::WebContents* web_contents) const {
  for (auto* container_view : contents_container_views_) {
    if (container_view->contents_view()->web_contents() == web_contents) {
      return container_view;
    }
  }
  return nullptr;
}

bool MultiContentsView::IsInSplitView() const {
  return resize_area_->GetVisible();
}

void MultiContentsView::SetWebContentsAtIndex(
    content::WebContents* web_contents,
    int index) {
  CHECK(index >= 0 &&
        index < static_cast<int>(contents_container_views_.size()));
  contents_container_views_[index]->contents_view()->SetWebContents(
      web_contents);

  if (index > 0 && !IsInSplitView()) {
    // Preserve the old behavior for callers that reveal split view by setting
    // the second web contents.
    CHECK_EQ(index, 1);
    SetSplitViewLayout(split_tabs::SplitTabLayout::kVertical, 2);
  }

  // For multi-pane layouts, visibility should be configured first via
  // SetSplitViewLayout().
  CHECK(contents_container_views_[index]->GetVisible());
}

void MultiContentsView::ShowSplitView(double ratio) {
  if (!IsInSplitView()) {
    // If split view is not visible, set the `start_ratio_` and update the view
    // visibility.
    start_ratio_ = ratio;
    SetSplitViewLayout(split_tabs::SplitTabLayout::kVertical, 2);
  } else if (start_ratio_ != ratio) {
    // If the split view is visible but ratio is changed, update the split
    // ratio.
    UpdateSplitRatio(ratio);
  }
  // Split view is visible and ratio is not changed, do nothing.
}

void MultiContentsView::CloseSplitView() {
  if (!IsInSplitView()) {
    return;
  }

  SetDragSwapTargetHighlightIndex(std::nullopt);

  if (active_index_ != 0) {
    ContentsContainerView* start_view = contents_container_views_[0];
    ContentsContainerView* active_view =
        contents_container_views_[active_index_];

    // Move the active WebContents so that the first ContentsContainerView in
    // contents_container_views_ can always be visible.
    std::iter_swap(contents_container_views_.begin(),
                   contents_container_views_.begin() + active_index_);

    // Reorder the child views so that focus order will be consistent with
    // contents_container_views_.
    size_t start_view_child_index = GetIndexOf(start_view).value();
    size_t active_view_child_index = GetIndexOf(active_view).value();
    ReorderChildView(start_view, active_view_child_index);
    ReorderChildView(active_view, start_view_child_index);

    active_index_ = 0;
  }

  SetSplitViewLayout(split_tabs::SplitTabLayout::kVertical, 1);
}

void MultiContentsView::SetActiveIndex(int index) {
  // Index should never be less than 0 or equal to or greater than the total
  // number of contents views.
  CHECK(index >= 0 && index < static_cast<int>(split_pane_count_));
  // We will only activate a visible contents view.
  CHECK(contents_container_views_[index]->GetVisible());
  active_index_ = index;
  for (size_t i = 0; i < contents_container_views_.size(); ++i) {
    contents_container_views_[i]
        ->contents_view()
        ->set_is_primary_web_contents_for_window(static_cast<int>(i) ==
                                                 active_index_);
  }
  UpdateContentsBorderAndOverlay();
}

void MultiContentsView::UpdateSplitRatio(double ratio) {
  if (start_ratio_ == ratio) {
    return;
  }

  start_ratio_ = ratio;
  InvalidateLayout();
}

void MultiContentsView::SetHighlightActiveContentsView(bool is_highlighted) {
  if (active_contents_view_highlighted_ != is_highlighted) {
    active_contents_view_highlighted_ = is_highlighted;
    UpdateContentsBorderAndOverlay();
  }
}

void MultiContentsView::ExecuteOnEachVisibleContentsView(
    base::RepeatingCallback<void(ContentsWebView*)> callback) {
  auto run_if_visible = [&](ContentsContainerView* container_view) {
    if (container_view->GetVisible()) {
      callback.Run(container_view->contents_view());
    }
  };

  run_if_visible(contents_container_views_[active_index_]);
  for (size_t i = 0; i < contents_container_views_.size(); ++i) {
    if (static_cast<int>(i) == active_index_) {
      continue;
    }
    run_if_visible(contents_container_views_[i]);
  }
}

void MultiContentsView::OnSwap() {
  CHECK(IsInSplitView());
  if (split_pane_count_ != 2U) {
    return;
  }
  delegate_->ReverseWebContents();
}

void MultiContentsView::SetSplitViewLayout(split_tabs::SplitTabLayout layout,
                                          size_t pane_count) {
  pane_count =
      std::clamp<size_t>(pane_count, 1U, contents_container_views_.size());

  split_layout_ = layout;
  split_pane_count_ = pane_count;

  if (active_index_ >= static_cast<int>(split_pane_count_)) {
    active_index_ = 0;
  }

  for (size_t i = 0; i < contents_container_views_.size(); ++i) {
    const bool visible = i < split_pane_count_;
    if (!visible) {
      contents_container_views_[i]->contents_view()->SetWebContents(nullptr);
    }
    contents_container_views_[i]->SetVisible(visible);
  }

  resize_area_->SetVisible(split_pane_count_ > 1);

  if (split_pane_count_ != 2U) {
    drag_swap_target_highlight_index_.reset();
    CHECK(drag_swap_indicator_);
    drag_swap_indicator_->SetVisible(false);
  }

  for (size_t i = 0; i < contents_container_views_.size(); ++i) {
    contents_container_views_[i]
        ->contents_view()
        ->set_is_primary_web_contents_for_window(static_cast<int>(i) ==
                                                 active_index_);
  }

  UpdateContentsBorderAndOverlay();
  InvalidateLayout();
}

void MultiContentsView::SetDragSwapTargetHighlightIndex(std::optional<int> index) {
  if (!IsInSplitView() || split_pane_count_ != 2U) {
    index = std::nullopt;
  } else if (index.has_value() &&
             (index.value() < 0 || index.value() >= 2)) {
    index = std::nullopt;
  }

  if (drag_swap_target_highlight_index_ == index) {
    return;
  }
  drag_swap_target_highlight_index_ = index;

  CHECK(drag_swap_indicator_);
  drag_swap_indicator_->SetVisible(drag_swap_target_highlight_index_.has_value());

  UpdateContentsBorderAndOverlay();
}

std::vector<views::View*> MultiContentsView::GetAccessiblePanes() {
  std::vector<views::View*> accessible_panes;
  for (auto* contents_container_view : contents_container_views_) {
    if (!contents_container_view->GetVisible()) {
      continue;
    }
    auto contents_accessible_panes =
        contents_container_view->GetAccessiblePanes();
    accessible_panes.insert(accessible_panes.end(),
                            contents_accessible_panes.begin(),
                            contents_accessible_panes.end());
  }
  return accessible_panes;
}

void MultiContentsView::OnResize(int resize_amount, bool done_resizing) {
  CHECK(IsInSplitView());

  auto* const start_column_view = contents_container_views_[0];
  ContentsContainerView* end_column_view = nullptr;
  switch (split_pane_count_) {
    case 2U:
      end_column_view = contents_container_views_[1];
      break;
    case 3U:
      end_column_view = (split_layout_ == split_tabs::SplitTabLayout::
                                            kThreePaneStartStacked)
                            ? contents_container_views_[2]
                            : contents_container_views_[1];
      break;
    case 4U:
      end_column_view = contents_container_views_[2];
      break;
    default:
      break;
  }
  CHECK(end_column_view);

  if (!initial_start_width_on_resize_.has_value()) {
    initial_start_width_on_resize_ =
        std::make_optional(start_column_view->size().width());
  }
  double total_width = start_column_view->size().width() +
                       start_column_view->GetInsets().width() +
                       end_column_view->size().width() +
                       end_column_view->GetInsets().width();
  double end_width = (initial_start_width_on_resize_.value() +
                      start_column_view->GetInsets().width() +
                      static_cast<double>(resize_amount));

  // If end_width is within the snap point widths, update to the snap point.
  delegate_->ResizeWebContents(
      CalculateRatioWithSnapPoints(end_width, total_width), done_resizing);

  if (done_resizing) {
    initial_start_width_on_resize_ = std::nullopt;
  }
}

double MultiContentsView::CalculateRatioWithSnapPoints(
    double end_width,
    double total_width) const {
  for (const double& snap_point : snap_points_) {
    double dp_snap_point = snap_point * total_width;
    if (std::abs(dp_snap_point - end_width) < kSnapDistance) {
      return snap_point;
    }
  }
  return end_width / total_width;
}

void MultiContentsView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateContentsBorderAndOverlay();
}

int MultiContentsView::GetInactiveIndex() const {
  if (!IsInSplitView()) {
    return active_index_;
  }
  for (int i = 0; i < static_cast<int>(split_pane_count_); ++i) {
    if (i != active_index_) {
      return i;
    }
  }
  return active_index_;
}

void MultiContentsView::OnWebContentsFocused(views::WebView* web_view) {
  if (!IsInSplitView() || !GetWidget()->IsVisible() || !web_view) {
    return;
  }

  // Check whether the widget is visible as otherwise during browser hide,
  // inactive web contents gets focus. See crbug.com/419335827
  content::WebContents* const focused_contents = web_view->web_contents();
  if (!focused_contents ||
      focused_contents == GetActiveContentsView()->web_contents()) {
    return;
  }

  delegate_->WebContentsFocused(focused_contents);
}

void MultiContentsView::OnActorOverlayFocused(views::WebView* web_view) {
  if (!IsInSplitView() || !GetWidget()->IsVisible() || !web_view) {
    return;
  }

  for (auto* contents_container_view : contents_container_views_) {
    if (!contents_container_view->GetVisible()) {
      continue;
    }
    if (contents_container_view->actor_overlay_web_view() == web_view) {
      content::WebContents* const contents =
          contents_container_view->contents_view()->web_contents();
      if (contents && contents != GetActiveContentsView()->web_contents()) {
        delegate_->WebContentsFocused(contents);
      }
      return;
    }
  }
}

void MultiContentsView::OnNtpFooterFocused(views::WebView* web_view) {
  if (!IsInSplitView() || !GetWidget()->IsVisible() || !web_view) {
    return;
  }

  for (auto* contents_container_view : contents_container_views_) {
    if (!contents_container_view->GetVisible()) {
      continue;
    }
    if (contents_container_view->new_tab_footer_view() == web_view) {
      content::WebContents* const contents =
          contents_container_view->contents_view()->web_contents();
      if (contents && contents != GetActiveContentsView()->web_contents()) {
        delegate_->WebContentsFocused(contents);
      }
      return;
    }
  }
}

// TODO(crbug.com/397777917): Consider using FlexSpecification weights and
// interior margins instead of a custom layout once this bug is resolved.
views::ProposedLayout MultiContentsView::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  views::ProposedLayout layouts;
  if (!size_bounds.is_fully_bounded()) {
    return layouts;
  }
  const int width = size_bounds.width().value();
  const int height = size_bounds.height().value();

  gfx::Rect available_space = gfx::Rect(width, height);

  const bool show_background =
      drop_target_view_->GetVisible() || IsInSplitView();
  layouts.child_layouts.emplace_back(background_view_.get(), show_background,
                                     available_space);

  if (IsDragAndDropEnabled()) {
    available_space =
        CalculateDropTargetLayout(available_space, layouts.child_layouts);
  }

  available_space =
      CalculateSeparatorLayouts(available_space, layouts.child_layouts);

  ViewWidths widths = GetViewWidths(available_space);

  gfx::Rect start_rect(available_space.origin(),
                       gfx::Size(widths.start_width, available_space.height()));
  gfx::Rect resize_rect(
      start_rect.top_right(),
      gfx::Size(widths.resize_width, available_space.height()));
  gfx::Rect end_rect(resize_rect.top_right(),
                     gfx::Size(widths.end_width, available_space.height()));

  if (IsInSplitView()) {
    start_rect.Inset(start_contents_view_inset_);
    end_rect.Inset(end_contents_view_inset_);
  }

  const int stack_gap = kSplitViewContentInset;
  const auto split_column = [stack_gap](const gfx::Rect& column_rect)
      -> std::pair<gfx::Rect, gfx::Rect> {
    if (column_rect.IsEmpty()) {
      return {gfx::Rect(), gfx::Rect()};
    }
    const int gap = std::min(stack_gap, column_rect.height());
    const int top_height = std::max(0, (column_rect.height() - gap) / 2);
    const int bottom_height =
        std::max(0, column_rect.height() - gap - top_height);
    gfx::Rect top(column_rect.origin(),
                  gfx::Size(column_rect.width(), top_height));
    gfx::Rect bottom(column_rect.x(), column_rect.y() + top_height + gap,
                     column_rect.width(), bottom_height);
    return {top, bottom};
  };

  std::array<gfx::Rect, 4> pane_bounds = {gfx::Rect(), gfx::Rect(), gfx::Rect(),
                                         gfx::Rect()};
  if (!IsInSplitView()) {
    pane_bounds[0] = start_rect;
  } else {
    const auto [start_top, start_bottom] = split_column(start_rect);
    const auto [end_top, end_bottom] = split_column(end_rect);

    switch (split_pane_count_) {
      case 2U:
        pane_bounds[0] = start_rect;
        pane_bounds[1] = end_rect;
        break;
      case 3U:
        if (split_layout_ ==
            split_tabs::SplitTabLayout::kThreePaneEndStacked) {
          pane_bounds[0] = start_rect;
          pane_bounds[1] = end_top;
          pane_bounds[2] = end_bottom;
        } else {
          pane_bounds[0] = start_top;
          pane_bounds[1] = start_bottom;
          pane_bounds[2] = end_rect;
        }
        break;
      case 4U:
        pane_bounds[0] = start_top;
        pane_bounds[1] = start_bottom;
        pane_bounds[2] = end_top;
        pane_bounds[3] = end_bottom;
        break;
      default:
        break;
    }
  }

  for (size_t i = 0; i < contents_container_views_.size(); ++i) {
    auto* const pane = contents_container_views_[i];
    layouts.child_layouts.emplace_back(pane, pane->GetVisible(),
                                       pane->GetVisible() ? pane_bounds[i]
                                                          : gfx::Rect());
  }

  layouts.child_layouts.emplace_back(resize_area_.get(),
                                     resize_area_->GetVisible(), resize_rect);

  gfx::Rect drag_swap_indicator_rect;
  if (IsInSplitView()) {
    drag_swap_indicator_rect = resize_rect;
    drag_swap_indicator_rect.ToCenteredSize(
        drag_swap_indicator_->GetPreferredSize());
  }
  layouts.child_layouts.emplace_back(drag_swap_indicator_.get(),
                                     drag_swap_indicator_->GetVisible(),
                                     drag_swap_indicator_rect);

  layouts.host_size = gfx::Size(width, height);
  return layouts;
}

gfx::Rect MultiContentsView::CalculateDropTargetLayout(
    const gfx::Rect& available_space,
    std::vector<views::ChildLayout>& child_layouts) const {
  CHECK(IsDragAndDropEnabled());
  if (!drop_target_view_->GetVisible()) {
    child_layouts.emplace_back(drop_target_view_.get(), false, gfx::Rect());
    return available_space;
  }

  const int drop_target_width =
      drop_target_view_->GetPreferredWidth(available_space.width());

  const int drop_target_x = (drop_target_view_->side() ==
                             MultiContentsDropTargetView::DropSide::START)
                                ? available_space.x()
                                : available_space.right() - drop_target_width;
  const int remaining_space_x =
      available_space.x() + ((drop_target_view_->side() ==
                              MultiContentsDropTargetView::DropSide::START)
                                 ? drop_target_width
                                 : 0);

  child_layouts.emplace_back(
      drop_target_view_.get(), true,
      gfx::Rect(drop_target_x, available_space.y(), drop_target_width,
                available_space.height()));

  return gfx::Rect(remaining_space_x, available_space.y(),
                   available_space.width() - drop_target_width,
                   available_space.height());
}

gfx::Rect MultiContentsView::CalculateSeparatorLayouts(
    const gfx::Rect& available_space,
    std::vector<views::ChildLayout>& child_layouts) const {
  if (IsInSplitView()) {
    child_layouts.emplace_back(contents_separators_.top_separator.get(), false,
                               gfx::Rect());
    child_layouts.emplace_back(contents_separators_.leading_separator.get(),
                               false, gfx::Rect());
    child_layouts.emplace_back(contents_separators_.trailing_separator.get(),
                               false, gfx::Rect());
    child_layouts.emplace_back(contents_separators_.corner_separator.get(),
                               false, gfx::Rect());
    return available_space;
  }

  const int width = available_space.width();
  const int height = available_space.height();

  const int separator_height =
      contents_separators_.should_show_top
          ? contents_separators_.top_separator->GetPreferredSize().height()
          : 0;
  child_layouts.emplace_back(
      contents_separators_.top_separator.get(),
      contents_separators_.should_show_top,
      gfx::Rect(available_space.origin(), {width, separator_height}));

  const bool should_show_leading =
      contents_separators_.should_show_leading ||
      (drop_target_view_->side() ==
       MultiContentsDropTargetView::DropSide::START);
  const int leading_separator_width =
      should_show_leading
          ? contents_separators_.leading_separator->GetPreferredSize().width()
          : 0;
  child_layouts.emplace_back(
      contents_separators_.leading_separator.get(), should_show_leading,
      gfx::Rect(available_space.origin(), {leading_separator_width, height}));

  const bool should_show_trailing =
      contents_separators_.should_show_trailing ||
      (drop_target_view_->side() == MultiContentsDropTargetView::DropSide::END);

  const int trailing_separator_width =
      should_show_trailing
          ? contents_separators_.trailing_separator->GetPreferredSize().width()
          : 0;
  child_layouts.emplace_back(
      contents_separators_.trailing_separator.get(), should_show_trailing,
      gfx::Rect(available_space.right() - trailing_separator_width,
                available_space.y(), trailing_separator_width, height));

  // Place the corner separator and set its orientation.
  auto* const corner_separator = contents_separators_.corner_separator.get();
  const auto corner_preferred_size = corner_separator->GetPreferredSize();
  views::ChildLayout corner_layout(
      corner_separator, contents_separators_.should_show_top &&
                            (should_show_leading || should_show_trailing));
  if (corner_layout.visible) {
    if (should_show_leading) {
      corner_layout.bounds =
          gfx::Rect(available_space.origin(), corner_preferred_size);
      corner_separator->SetOrientation(
          CustomFloatingCorner::CornerOrientation::kTopLeading);
    } else {
      corner_layout.bounds = gfx::Rect(
          gfx::Point(available_space.right() - corner_preferred_size.width(),
                     available_space.y()),
          corner_preferred_size);
      corner_separator->SetOrientation(
          CustomFloatingCorner::CornerOrientation::kTopTrailing);
    }
  }
  child_layouts.push_back(corner_layout);

  return gfx::Rect(available_space.x() + leading_separator_width,
                   available_space.y() + separator_height,
                   width - trailing_separator_width - leading_separator_width,
                   height - separator_height);
}

MultiContentsView::ViewWidths MultiContentsView::GetViewWidths(
    gfx::Rect available_space) const {
  ViewWidths widths;
  if (IsInSplitView()) {
    CHECK_GT(split_pane_count_, 1U);
    widths.resize_width = resize_area_->GetPreferredSize().width();
    widths.start_width =
        start_ratio_ * (available_space.width() - widths.resize_width);
    widths.end_width =
        available_space.width() - widths.start_width - widths.resize_width;
  } else {
    CHECK_EQ(split_pane_count_, 1U);
    widths.start_width = available_space.width();
  }
  return ClampToMinWidth(available_space, widths);
}

MultiContentsView::ViewWidths MultiContentsView::ClampToMinWidth(
    gfx::Rect available_space,
    ViewWidths widths) const {
  if (!IsInSplitView()) {
    // Don't clamp if in a single-view state, where other views should be 0
    // width.
    return widths;
  }

  const int min_width = GetMinViewWidth(available_space);
  if (widths.start_width < min_width) {
    const double diff = min_width - widths.start_width;
    widths.start_width += diff;
    widths.end_width -= diff;
  } else if (widths.end_width < min_width) {
    const double diff = min_width - widths.end_width;
    widths.end_width += diff;
    widths.start_width -= diff;
  }
  return widths;
}

int MultiContentsView::GetMinViewWidth(gfx::Rect available_space) const {
  CHECK(IsInSplitView());

  // The minimum width for a content view in a split should be the lesser of
  // kMinWebContentsWidth, and kMinWebContentsWidthPercentage as a percentage of
  // the MultiContentsView's available width with a lower bound of
  // kConstrainedMinWebContentsWidth.
  const int min_percentage =
      kMinWebContentsWidthPercentage * available_space.width();
  const int min_fixed_value =
      min_contents_width_for_testing_.value_or(kMinWebContentsWidth);
  return std::min(min_fixed_value,
                  std::max(kConstrainedMinWebContentsWidth, min_percentage));
}

void MultiContentsView::UpdateContentsBorderAndOverlay() {
  for (size_t i = 0; i < contents_container_views_.size(); ++i) {
    auto* const contents_container_view = contents_container_views_[i];
    const bool is_visible_pane = contents_container_view->GetVisible();
    const bool is_active =
        is_visible_pane && static_cast<int>(i) == active_index_;

    bool is_highlighted =
        is_active && active_contents_view_highlighted_ && is_visible_pane;
    if (drag_swap_target_highlight_index_.has_value() &&
        split_pane_count_ == 2U) {
      is_highlighted = is_visible_pane &&
                       static_cast<int>(i) ==
                           drag_swap_target_highlight_index_.value();
    }

    contents_container_view->UpdateBorderAndOverlay(
        IsInSplitView() && is_visible_pane, is_active, is_highlighted);
  }
}

MultiContentsViewDropTargetController&
MultiContentsView::drop_target_controller() const {
  CHECK(IsDragAndDropEnabled());
  return *drop_target_controller_;
}

bool MultiContentsView::IsDragAndDropEnabled() const {
  // Split view drag and drop is only supported on normal browser types.
  if (!browser_view_->GetIsNormalType() || !is_drag_drop_pref_enabled_) {
    return false;
  }

  const auto* active_contents_view = GetActiveContentsView();
  if (!active_contents_view) {
    return true;
  }

  const auto* web_contents = active_contents_view->web_contents();
  return !web_contents ||
         web_contents->GetLastCommittedURL().spec() ==
             chrome::kChromeUINewTabURL ||
         web_contents->GetLastCommittedURL().spec() ==
             chrome::kChromeUINewTabPageURL ||
         !web_contents->GetLastCommittedURL().SchemeIs(
             content::kChromeUIScheme);
}

void MultiContentsView::OnDragAndDropPrefStateChange() {
  is_drag_drop_pref_enabled_ =
      browser_view_->GetProfile()->GetPrefs()->GetBoolean(
          prefs::kSplitViewDragAndDropEnabled);
  InvalidateLayout();
}

void MultiContentsView::SetShouldShowTopSeparator(bool should_show) {
  if (contents_separators_.should_show_top == should_show) {
    return;
  }
  contents_separators_.should_show_top = should_show;
  start_contents_view_inset_.set_top(
      should_show ? 0 : MultiContentsView::kSplitViewContentInset);
  end_contents_view_inset_.set_top(
      should_show ? 0 : MultiContentsView::kSplitViewContentInset);

  // This can be called during BrowserView layout, so protect against creating a
  // layout loop.
  InvalidateLayout(/*avoid_propagate_during_layout=*/true);
}

void MultiContentsView::SetShouldShowLeadingSeparator(bool should_show) {
  if (contents_separators_.should_show_leading == should_show) {
    return;
  }
  contents_separators_.should_show_leading = should_show;
  start_contents_view_inset_.set_left(
      should_show ? 0 : MultiContentsView::kSplitViewContentInset);

  // This can be called during BrowserView layout, so protect against creating a
  // layout loop.
  InvalidateLayout(/*avoid_propagate_during_layout=*/true);
}

void MultiContentsView::SetShouldShowTrailingSeparator(bool should_show) {
  if (contents_separators_.should_show_trailing == should_show) {
    return;
  }
  contents_separators_.should_show_trailing = should_show;
  end_contents_view_inset_.set_right(
      should_show ? 0 : MultiContentsView::kSplitViewContentInset);

  // This can be called during BrowserView layout, so protect against creating a
  // layout loop.
  InvalidateLayout(/*avoid_propagate_during_layout=*/true);
}

BEGIN_METADATA(MultiContentsView)
END_METADATA
