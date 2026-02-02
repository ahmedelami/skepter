// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"

#include <algorithm>
#include <optional>
#include <variant>

#include "base/callback_list.h"
#include "base/containers/adapters.h"
#include "base/functional/bind.h"
#include "base/notimplemented.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_api/tab_strip_service.h"
#include "chrome/browser/ui/tabs/tab_strip_api/tab_strip_service_feature.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/app_menu_button.h"
#include "chrome/browser/ui/views/frame/custom_corners_background.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/tabs/vertical/root_tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/vertical/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_drag_handler.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_flat_edge_button.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_unpinned_tab_container_view.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/url_formatter/url_formatter.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/models/image_model.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/views/background.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "url/url_constants.h"

namespace {
constexpr int kRegionVerticalPadding = 5;
constexpr bool kSkepterShowSidebarUrlRow = false;
#if BUILDFLAG(IS_MAC)
// macOS: keep the vertical tab strip sidebar "tabs-only".
constexpr bool kSkepterSidebarTabsOnly = true;
constexpr bool kSkepterShowSidebarNewTabButton = false;
#else
constexpr bool kSkepterSidebarTabsOnly = false;
constexpr bool kSkepterShowSidebarNewTabButton = true;
#endif

const url_formatter::FormatUrlType kUrlFormatFlags =
    url_formatter::kFormatUrlOmitDefaults |
    url_formatter::kFormatUrlOmitTrivialSubdomains |
    url_formatter::kFormatUrlOmitHTTPS | url_formatter::kFormatUrlTrimAfterHost;

bool IsNewTabPageUrl(const GURL& url) {
  if (!url.SchemeIs(content::kChromeUIScheme)) {
    return false;
  }
  return url.host() == chrome::kChromeUINewTabHost ||
         url.host() == chrome::kChromeUINewTabPageHost;
}
}  // namespace

VerticalTabStripRegionView::VerticalTabStripRegionView(
    tabs::VerticalTabStripStateController* state_controller,
    actions::ActionItem* root_action_item,
    BrowserView* browser_view)
    : browser_view_(browser_view),
      tab_strip_model_(browser_view->browser()->GetTabStripModel()),
      state_controller_(state_controller),
      resize_animation_(this) {
  // For z-ordering purposes this needs to be on a layer.
  SetPaintToLayer();
  // Because corners may be transparent, this must be set to false.
  layer()->SetFillsBoundsOpaquely(false);

  flex_layout_ = SetLayoutManager(std::make_unique<views::FlexLayout>());
  flex_layout_->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCollapseMargins(true)
      .SetDefault(
          views::kFlexBehaviorKey,
          views::FlexSpecification(views::LayoutOrientation::kVertical,
                                   views::MinimumFlexSizeRule::kPreferred,
                                   views::MaximumFlexSizeRule::kPreferred));

  // Create child views.
  top_button_container_ =
      AddChildView(std::make_unique<VerticalTabStripTopContainer>(
          state_controller_, root_action_item, browser_view->browser()));

  url_row_container_ = AddChildView(std::make_unique<views::View>());
  auto* url_row_layout =
      url_row_container_->SetLayoutManager(std::make_unique<views::FlexLayout>());
  url_row_layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCollapseMargins(true)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch);

  url_row_page_info_button_ =
      url_row_container_->AddChildView(std::make_unique<VerticalTabStripFlatEdgeButton>());
  url_row_page_info_button_->SetCallback(base::BindRepeating(
      &VerticalTabStripRegionView::OnUrlRowPageInfoPressed,
      base::Unretained(this)));
  url_row_page_info_button_->SetText(std::u16string());
  url_row_page_info_button_->SetTooltipText(u"Page info");
  url_row_page_info_button_->SetFlatEdge(
      VerticalTabStripFlatEdgeButton::FlatEdge::kRight);
  const gfx::Insets url_row_insets = GetLayoutInsets(
      LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_UNCOLLAPSED);
  constexpr int kUrlRowInnerPadding = 6;
  url_row_page_info_button_->SetInsets(gfx::Insets::TLBR(
      url_row_insets.top(), url_row_insets.left(), url_row_insets.bottom(),
      kUrlRowInnerPadding));

  url_row_button_ =
      url_row_container_->AddChildView(std::make_unique<VerticalTabStripFlatEdgeButton>());
  url_row_button_->SetCallback(base::BindRepeating(
      &VerticalTabStripRegionView::OnUrlRowPressed, base::Unretained(this)));
  url_row_button_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  url_row_button_->SetElideBehavior(gfx::ElideBehavior::ELIDE_TAIL);
  url_row_button_->SetInsets(gfx::Insets::TLBR(
      url_row_insets.top(), kUrlRowInnerPadding, url_row_insets.bottom(),
      url_row_insets.right()));
  url_row_button_->SetFlatEdge(VerticalTabStripFlatEdgeButton::FlatEdge::kLeft);
  url_row_button_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  top_button_separator_ = AddChildView(std::make_unique<views::Separator>());

  new_tab_button_container_ =
      AddChildView(std::make_unique<VerticalTabStripBottomContainer>(
          state_controller_, root_action_item, browser_view->browser(),
          VerticalTabStripBottomContainer::ButtonSet::kNewTabOnly));
  new_tab_button_container_->SetVisible(kSkepterShowSidebarNewTabButton);

  bottom_button_container_ =
      AddChildView(std::make_unique<VerticalTabStripBottomContainer>(
          state_controller_, root_action_item, browser_view->browser(),
          VerticalTabStripBottomContainer::ButtonSet::kTabGroupOnly));

  gemini_button_ = AddChildView(std::make_unique<views::View>());

  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));
  resize_area_->SetProperty(views::kViewIgnoredByLayoutKey, true);

  resize_animation_.SetSlideDuration(
      gfx::Animation::RichAnimationDuration(base::Milliseconds(450)));
  resize_animation_.SetTweenType(gfx::Tween::Type::EASE_IN_OUT_EMPHASIZED);
  resize_animation_.Reset(!state_controller_->IsCollapsed());

  target_collapse_state_ = state_controller_->GetState();
  OnCollapsedStateChanged(state_controller_);
  collapsed_state_changed_subscription_ =
      state_controller_->RegisterOnCollapseChanged(base::BindRepeating(
          &VerticalTabStripRegionView::OnCollapsedStateChanged,
          base::Unretained(this)));

  SetProperty(views::kElementIdentifierKey, kVerticalTabStripRegionElementId);

  GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);

  SetBackground(std::make_unique<CustomCornersBackground>(
      *this, *browser_view,
      /*primary_color=*/CustomCornersBackground::FrameColor(),
      /*corner_color=*/CustomCornersBackground::TopContainerTheme()));

  UpdateColors();

  UpdateUrlRow(browser_view_ ? browser_view_->GetActiveWebContents() : nullptr);
}

VerticalTabStripRegionView::~VerticalTabStripRegionView() {
  if (root_node_) {
    root_node_->SetController(nullptr);
  }

  tab_strip_controller_.reset();

  if (drag_handler_) {
    auto handler = RemoveChildViewT(drag_handler_->GetDragContext());
    drag_handler_ = nullptr;
  }
}

std::optional<double> VerticalTabStripRegionView::GetCollapseAnimationPercent()
    const {
  return resize_animation_.is_animating()
             ? std::make_optional(resize_animation_.GetCurrentValue())
             : std::nullopt;
}

void VerticalTabStripRegionView::AddedToWidget() {
  paint_as_active_subscription_ =
      GetWidget()->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
          &VerticalTabStripRegionView::UpdateColors, base::Unretained(this)));

  UpdateUrlRow(browser_view_ ? browser_view_->GetActiveWebContents() : nullptr);

  MaybeMoveProfileAndAppMenuButtons();
}

void VerticalTabStripRegionView::Layout(PassKey) {
  LayoutSuperclass<views::AccessiblePaneView>(this);

  // Manually position the resize area as it overlaps views handled by the flex
  // layout.
  resize_area_->SetBoundsRect(gfx::Rect(bounds().right() - kResizeAreaWidth, 0,
                                        kResizeAreaWidth, bounds().height()));
}

views::View* VerticalTabStripRegionView::GetDefaultFocusableChild() {
  if (top_button_container_ && top_button_container_->GetVisible()) {
    // Only focus the top container when it actually contains focusable
    // controls.
    if (auto* collapse = top_button_container_->GetCollapseButton();
        collapse && collapse->GetVisible()) {
      return top_button_container_;
    }
#if !BUILDFLAG(IS_MAC)
    if (auto* tab_search = top_button_container_->GetTabSearchButton();
        tab_search && tab_search->GetVisible()) {
      return top_button_container_;
    }
#endif
  }
  if (url_row_button_ && url_row_button_->GetVisible()) {
    return url_row_button_;
  }
  if (new_tab_button_container_ && new_tab_button_container_->GetVisible()) {
    return new_tab_button_container_;
  }
  if (tab_strip_view_ && tab_strip_view_->GetVisible()) {
    return tab_strip_view_;
  }
  if (bottom_button_container_ && bottom_button_container_->GetVisible()) {
    return bottom_button_container_;
  }
  return nullptr;
}

void VerticalTabStripRegionView::InitializeTabStrip() {
  if (root_node_) {
    return;
  }

  root_node_ = std::make_unique<RootTabCollectionNode>(
      tab_strip_model_,
      base::BindRepeating(&VerticalTabStripRegionView::SetTabStripView,
                          base::Unretained(this)),
      base::BindRepeating(&VerticalTabStripRegionView::ClearTabStripView,
                          base::Unretained(this)));

  std::unique_ptr<TabMenuModelFactory> tab_menu_model_factory;
  if (browser_view_ && browser_view_->browser()->app_controller()) {
    tab_menu_model_factory =
        browser_view_->browser()->app_controller()->GetTabMenuModelFactory();
  }

  TabStripModel* tab_strip_model = browser_view_->browser()->GetTabStripModel();
  CHECK(tab_strip_model);
  auto drag_handler = std::make_unique<VerticalTabDragHandlerImpl>(
      *tab_strip_model, *root_node_.get());
  drag_handler_ = drag_handler.get();

  CHECK(!tab_strip_controller_);
  tab_strip_controller_ = std::make_unique<VerticalTabStripController>(
      tab_strip_model, browser_view_, *AddChildView(std::move(drag_handler)),
      std::move(tab_menu_model_factory));

  root_node_->SetController(tab_strip_controller_.get());

  root_node_->Init();
}

void VerticalTabStripRegionView::ResetTabStrip() {
  if (!root_node_) {
    return;
  }

  root_node_->Reset();

  root_node_->SetController(nullptr);
  tab_strip_controller_.reset();

  CHECK(drag_handler_);
  auto* drag_handler = drag_handler_.get();
  drag_handler_ = nullptr;
  RemoveChildViewT(drag_handler->GetDragContext());

  root_node_.reset();
}

gfx::Size VerticalTabStripRegionView::GetMinimumSize() const {
  auto min_size = TabStripRegionView::GetMinimumSize();
  if (state_controller_->IsZenHidden() || starting_width_on_resize_.has_value()) {
    min_size.set_width(kZenHiddenWidth);
    return min_size;
  }
  min_size.set_width(
      (state_controller_->IsCollapsed() || resize_animation_.is_animating())
          ? kCollapsedWidth
          : kUncollapsedMinWidth);
  return min_size;
}

gfx::Size VerticalTabStripRegionView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  auto size = TabStripRegionView::CalculatePreferredSize(available_size);
  if (target_collapse_state_.zen_hidden) {
    size.set_width(kZenHiddenWidth);
    return size;
  }
  if (resize_animation_.is_animating()) {
    size.set_width(kCollapsedWidth +
                   base::ClampRound((target_collapse_state_.uncollapsed_width -
                                     kCollapsedWidth) *
                                    resize_animation_.GetCurrentValue()));
  } else {
    size.set_width(target_collapse_state_.collapsed
                       ? kCollapsedWidth
                       : target_collapse_state_.uncollapsed_width);
  }
  return size;
}

bool VerticalTabStripRegionView::IsTabStripEditable() const {
  // TODO(crbug.com/467710547): This needs to consider the drag context. Wait
  // until that is implemented before updating this function.
  NOTIMPLEMENTED();
  return tab_strip_editable_for_testing_;
}

void VerticalTabStripRegionView::DisableTabStripEditingForTesting() const {
  // TODO(crbug.com/467710617): Implement this in VerticalTabStripView.
  NOTIMPLEMENTED();
}

bool VerticalTabStripRegionView::IsTabStripCloseable() const {
  // TODO(crbug.com/467710547): Return TabDragContext::IsTabStripCloseable once
  // it exists.
  NOTIMPLEMENTED();
  return true;
}

bool VerticalTabStripRegionView::IsAnimating() const {
  // TODO(crbug.com/467710547): Return if the view or drag context is animating
  // something.
  NOTIMPLEMENTED();
  return true;
}

void VerticalTabStripRegionView::StopAnimating() {
  // TODO(crbug.com/467710547): Stop any ongoing animation in the
  // VerticalTabStripView.
  NOTIMPLEMENTED();
}

void VerticalTabStripRegionView::UpdateLoadingAnimations(
    const base::TimeDelta& elapsed_time) {
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    const TabCollectionNode* node =
        root_node_->GetNodeForHandle(tab->GetHandle());
    VerticalTabView* tab_view =
        views::AsViewClass<VerticalTabView>(node->view());
    CHECK(tab_view);
    tab_view->StepLoadingAnimation(elapsed_time);
  }
}

std::optional<int> VerticalTabStripRegionView::GetFocusedTabIndex() const {
  const views::FocusManager* focus_manager = GetFocusManager();
  if (!focus_manager) {
    return std::nullopt;
  }

  const views::View* focused_view = focus_manager->GetFocusedView();
  if (!focused_view) {
    return std::nullopt;
  }

  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    const TabCollectionNode* node =
        root_node_->GetNodeForHandle(tab->GetHandle());
    if (node && node->view() == focused_view) {
      return i;
    }
  }

  return std::nullopt;
}

const TabRendererData& VerticalTabStripRegionView::GetTabRendererData(
    int tab_index) {
  tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(tab_index);
  CHECK(tab);

  const TabCollectionNode* node =
      root_node_->GetNodeForHandle(tab->GetHandle());
  CHECK(node);

  VerticalTabView* tab_view = views::AsViewClass<VerticalTabView>(node->view());
  CHECK(tab_view);

  return tab_view->tab_data();
}

views::View* VerticalTabStripRegionView::GetTabAnchorViewAt(int tab_index) {
  tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(tab_index);
  CHECK(tab) << "No tab found for tab_index: " << tab_index;

  const TabCollectionNode* node =
      root_node_->GetNodeForHandle(tab->GetHandle());
  CHECK(node) << "No node found for tab handle";

  return node->view();
}

views::View* VerticalTabStripRegionView::GetTabGroupAnchorView(
    const tab_groups::TabGroupId& group) {
  if (!tab_strip_model_->SupportsTabGroups()) {
    return nullptr;
  }

  if (const TabGroup* tab_group =
          tab_strip_model_->group_model()->GetTabGroup(group)) {
    return root_node_->GetNodeForHandle(tab_group->GetCollectionHandle())
        ->view();
  }

  return nullptr;
}

void VerticalTabStripRegionView::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  // TODO(crbug.com/479232024): Implement this.
}

TabDragContext* VerticalTabStripRegionView::GetDragContext() {
  return drag_handler_->GetDragContext();
}

std::optional<BrowserRootView::DropIndex>
VerticalTabStripRegionView::GetDropIndex(const ui::DropTargetEvent& event) {
  return std::nullopt;
}

BrowserRootView::DropTarget* VerticalTabStripRegionView::GetDropTarget(
    gfx::Point loc_in_local_coords) {
  return nullptr;
}

views::View* VerticalTabStripRegionView::GetViewForDrop() {
  return nullptr;
}

void VerticalTabStripRegionView::SetTabStripObserver(
    TabStripObserver* observer) {
  // Do nothing.
}

views::View* VerticalTabStripRegionView::GetTabStripView() {
  return tab_strip_view_;
}

void VerticalTabStripRegionView::UpdateUrlRow(content::WebContents* contents) {
  if (!url_row_button_ || !url_row_page_info_button_) {
    return;
  }

  if (!contents) {
    url_row_button_->SetText(std::u16string());
    url_row_button_->SetTooltipText(std::u16string());
    url_row_page_info_button_->SetTooltipText(std::u16string());
    return;
  }

  if (browser_view_) {
    LocationBarView* const location_bar = browser_view_->GetLocationBarView();
    LocationBarModel* const model =
        location_bar ? location_bar->GetLocationBarModel() : nullptr;
    if (model) {
      url_row_page_info_button_->UpdateIcon(
          ui::ImageModel::FromVectorIcon(model->GetVectorIcon()));
      const std::u16string display_text = model->GetURLForDisplay();
      if (!display_text.empty()) {
        url_row_button_->SetText(display_text);
        url_row_button_->SetTooltipText(model->GetFormattedFullURL());
        return;
      }
    }
  }

  GURL url = contents->GetVisibleURL();
  if (const GURL& last_committed_url = contents->GetLastCommittedURL();
      last_committed_url.is_valid()) {
    url = last_committed_url;
  }

  std::u16string display_text;
  if (!url.is_valid() || url.is_empty() || IsNewTabPageUrl(url)) {
    display_text = u"Type in here";
  } else if (url.SchemeIsFile()) {
    display_text = l10n_util::GetStringUTF16(IDS_HOVER_CARD_FILE_URL_SOURCE);
  } else if (url.SchemeIsBlob()) {
    display_text = l10n_util::GetStringUTF16(IDS_HOVER_CARD_BLOB_URL_SOURCE);
  } else if (url.SchemeIs(url::kViewSourceScheme)) {
    display_text =
        l10n_util::GetStringUTF16(IDS_HOVER_CARD_VIEW_SOURCE_URL_SOURCE);
  } else {
    display_text = url_formatter::FormatUrl(
        url, kUrlFormatFlags, base::UnescapeRule::SPACES, nullptr, nullptr,
        nullptr);
  }

  url_row_button_->SetText(display_text);
  url_row_button_->SetTooltipText(base::UTF8ToUTF16(url.spec()));
}

void VerticalTabStripRegionView::OnUrlRowPressed() {
  if (!browser_view_) {
    return;
  }

  // Defer to avoid re-entrancy while processing the click, since focusing the
  // location bar can transiently reparent views (Skepter omnibox popup).
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserView::SetFocusToLocationBar,
                                browser_view_->GetAsWeakPtr(),
                                /*is_user_initiated=*/true));
}

void VerticalTabStripRegionView::OnUrlRowPageInfoPressed() {
  if (!browser_view_ || !url_row_page_info_button_) {
    return;
  }

  content::WebContents* const web_contents = browser_view_->GetActiveWebContents();
  if (!web_contents) {
    return;
  }

  gfx::NativeWindow parent_window;
  if (browser_view_->GetWidget()) {
    parent_window = browser_view_->GetWidget()->GetNativeWindow();
  }
  if (!parent_window) {
    return;
  }

  GURL url;
  if (LocationBarView* const location_bar = browser_view_->GetLocationBarView()) {
    if (LocationBarModel* const model = location_bar->GetLocationBarModel()) {
      url = model->GetURL();
    }
  }
  if (!url.is_valid()) {
    url = web_contents->GetVisibleURL();
  }

  PageInfoBubbleSpecification::Builder builder(url_row_page_info_button_,
                                               parent_window, web_contents,
                                               url);
  views::BubbleDialogDelegateView* const bubble =
      PageInfoBubbleView::CreatePageInfoBubble(builder.Build());
  if (!bubble || !bubble->GetWidget()) {
    return;
  }
  bubble->SetArrow(views::BubbleBorder::LEFT_TOP);
  bubble->GetWidget()->Show();
}

void VerticalTabStripRegionView::OnResize(int resize_amount,
                                          bool done_resizing) {
  if (!starting_width_on_resize_.has_value()) {
    starting_width_on_resize_ = width();
  }
  const int proposed_width = starting_width_on_resize_.value() + resize_amount;
  if (done_resizing) {
    starting_width_on_resize_ = std::nullopt;
  }

  tabs::VerticalTabStripState new_state;
  new_state.zen_hidden = proposed_width < kZenHiddenSnapWidth;
  if (proposed_width > kCollapseSnapWidth) {
    new_state.collapsed = false;
    new_state.uncollapsed_width =
        std::clamp(proposed_width, kUncollapsedMinWidth, kUncollapsedMaxWidth);
    if (done_resizing) {
      // We only want to save the uncollapsed width to the state controller if
      // the user has lifted their mouse, otherwise dragging the resize area to
      // collapse will cause a subsequent collapse button click to only expand
      // to the minimum expanded width, and not to the starting width of the
      // drag-to-collapse operation.
      state_controller_->SetUncollapsedWidth(new_state.uncollapsed_width);
    }
  } else {
    new_state.collapsed = true;
    new_state.uncollapsed_width = target_collapse_state_.uncollapsed_width;
  }

  UpdateCollapseState(new_state);
}

void VerticalTabStripRegionView::AnimationProgressed(
    const gfx::Animation* animation) {
  DCHECK_EQ(animation, &resize_animation_);
  if (target_collapse_state_.zen_hidden) {
    return;
  }
  double width = kCollapsedWidth +
                 (target_collapse_state_.uncollapsed_width - kCollapsedWidth) *
                     resize_animation_.GetCurrentValue();
  ResizeToWidth((resize_animation_.IsShowing() ? std::floor<double>
                                               : std::ceil<double>)(width));
}

bool VerticalTabStripRegionView::IsPositionInWindowCaption(
    const gfx::Point& point) {
  // Check the resize area first, it should always take precedence over other
  // children regardless of order.
  if (IsHitInView(resize_area_, point)) {
    return false;
  }

  for (views::View* child : children()) {
    if (!child->GetVisible()) {
      continue;
    }
    gfx::Point point_in_child = point;
    views::View::ConvertPointToTarget(this, child, &point_in_child);
    if (child->HitTestPoint(point_in_child)) {
      if (child == top_button_container_) {
        return top_button_container_->IsPositionInWindowCaption(point_in_child);
      }
      if (child == tab_strip_view_) {
        return tab_strip_view_->IsPositionInWindowCaption(point_in_child);
      }
      return false;
    }
  }
  return true;
}

void VerticalTabStripRegionView::SetToolbarHeightForLayout(
    const int toolbar_height) {
  top_button_container_->SetToolbarHeightForLayout(toolbar_height);
}

void VerticalTabStripRegionView::SetExclusionWidthForLayout(
    const int exclusion_width) {
  exclusion_width_ = exclusion_width;
  top_button_container_->SetExclusionWidthForLayout(exclusion_width);
}

VerticalPinnedTabContainerView*
VerticalTabStripRegionView::GetPinnedTabsContainer() {
  return tab_strip_view_->GetPinnedTabsContainer();
}

VerticalUnpinnedTabContainerView*
VerticalTabStripRegionView::GetUnpinnedTabsContainer() {
  return tab_strip_view_->GetUnpinnedTabsContainer();
}

views::View* VerticalTabStripRegionView::SetTabStripView(
    std::unique_ptr<views::View> view) {
  CHECK(views::IsViewClass<VerticalTabStripView>(view.get()));
  tab_strip_view_ =
      static_cast<VerticalTabStripView*>(AddChildView(std::move(view)));
  OnCollapsedStateChanged(state_controller_);
  tab_strip_view_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kUnbounded));
  tab_strip_view_->SetProperty(
      views::kMarginsKey,
#if BUILDFLAG(IS_MAC)
      gfx::Insets::TLBR(0, 0, kRegionVerticalPadding, 0)
#else
      gfx::Insets::VH(kRegionVerticalPadding, 0)
#endif
  );
  std::optional<size_t> separator_index = GetIndexOf(top_button_separator_);
  CHECK(separator_index.has_value());
  // Keep the new tab button directly below the top separator, and the tab
  // strip directly below the new tab button.
  ReorderChildView(new_tab_button_container_, separator_index.value() + 1);
  std::optional<size_t> new_tab_index = GetIndexOf(new_tab_button_container_);
  CHECK(new_tab_index.has_value());
  ReorderChildView(tab_strip_view_, new_tab_index.value() + 1);
  return tab_strip_view_;
}

void VerticalTabStripRegionView::ClearTabStripView(views::View* view) {
  CHECK(tab_strip_view_);
  CHECK(tab_strip_view_ == view);
  RemoveChildViewT(std::exchange(tab_strip_view_, nullptr));
}

void VerticalTabStripRegionView::OnCollapsedStateChanged(
    tabs::VerticalTabStripStateController* state_controller) {
  const bool zen_hidden = state_controller->IsZenHidden();
  if (kSkepterSidebarTabsOnly) {
    // Tabs-only sidebar: never show other controls.
    top_button_container_->SetVisible(false);
    top_button_separator_->SetVisible(false);
    url_row_container_->SetVisible(false);
    new_tab_button_container_->SetVisible(false);
    bottom_button_container_->SetVisible(false);
    gemini_button_->SetVisible(false);
  } else {
    top_button_container_->SetVisible(!zen_hidden);
    top_button_separator_->SetVisible(!zen_hidden);
    const bool show_url_row = kSkepterShowSidebarUrlRow && !zen_hidden &&
                              !state_controller->IsCollapsed();
    url_row_container_->SetVisible(show_url_row);
    new_tab_button_container_->SetVisible(kSkepterShowSidebarNewTabButton &&
                                          !zen_hidden);
    bottom_button_container_->SetVisible(!zen_hidden);
    gemini_button_->SetVisible(!zen_hidden);
  }
  if (tab_strip_view_) {
    tab_strip_view_->SetVisible(!zen_hidden);
  }
  if (drag_handler_) {
    drag_handler_->GetDragContext()->SetVisible(!zen_hidden);
  }

  UpdateSkepterProfileAndAppMenuVisibility();

  if (target_collapse_state_.collapsed != state_controller->IsCollapsed() ||
      target_collapse_state_.zen_hidden != state_controller->IsZenHidden()) {
    // UpdateCollapseState is responsible for setting the collapsed state of the
    // state controller due to a resizing operation. To avoid reentrancy in that
    // case, only update the collapse state if the state controller's collapse
    // state is different than the current target collapse state, which can
    // happen due to the collapse button being pressed.
    UpdateCollapseState(state_controller_->GetState());
  }

  const int padding = GetLayoutConstant(
      state_controller_->IsCollapsed()
          ? LayoutConstant::kVerticalTabStripCollapsedPadding
          : LayoutConstant::kVerticalTabStripUncollapsedPadding);
  // The TopContainer handles the padding distance to the separator so that we
  // can control how far it is in the various states.
  top_button_separator_->SetProperty(views::kMarginsKey,
                                     gfx::Insets::VH(0, padding));
  if (state_controller->IsCollapsed()) {
    // If the VT Strip is collapsed, then we need exactly |padding| on the top,
    // left, and right.
    top_button_container_->SetProperty(
        views::kMarginsKey,
        gfx::Insets::TLBR(padding, padding, kRegionVerticalPadding, padding));
  } else {
    // If the VT Strip is not collapsed, then we want to align the heights of
    // the TopContainer w/ the the height of the toolbar. Both of these
    // components start at the top of the window. We keep no vertical padding so
    // that the separator can lay adjacent to it.
    top_button_container_->SetProperty(views::kMarginsKey,
                                       gfx::Insets::VH(0, padding));
  }

  url_row_container_->SetProperty(views::kMarginsKey,
                                  gfx::Insets::TLBR(0, padding, 0, padding));

  new_tab_button_container_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(kRegionVerticalPadding, padding, 0, padding));

  bottom_button_container_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(kRegionVerticalPadding, padding, 0, padding));

  flex_layout_->SetInteriorMargin(gfx::Insets::TLBR(
      0, 0,
      GetLayoutConstant(LayoutConstant::kVerticalTabStripUncollapsedPadding),
      0));

  if (tab_strip_view_) {
    tab_strip_view_->SetCollapsedState(state_controller->IsCollapsed());
  }
}

void VerticalTabStripRegionView::UpdateCollapseState(
    tabs::VerticalTabStripState new_state) {
  bool previously_collapsed = target_collapse_state_.collapsed;
  bool previously_zen_hidden = target_collapse_state_.zen_hidden;
  target_collapse_state_ = new_state;
  if (previously_zen_hidden != target_collapse_state_.zen_hidden) {
    if (target_collapse_state_.zen_hidden && resize_animation_.is_animating()) {
      resize_animation_.Stop();
    }
    state_controller_->SetZenHidden(target_collapse_state_.zen_hidden);
    InvalidateLayout();
  }
  if (previously_collapsed != target_collapse_state_.collapsed) {
    if (target_collapse_state_.collapsed) {
      resize_animation_.Hide();
    } else {
      resize_animation_.Show();
    }
    state_controller_->SetCollapsed(target_collapse_state_.collapsed);
    // This may change the minimum size of the top container.
    InvalidateLayout();
  } else if (!target_collapse_state_.collapsed &&
             !target_collapse_state_.zen_hidden &&
             !resize_animation_.is_animating()) {
    // If we are still in the expanding animation, resizing to the updated
    // uncollapsed width will happen in AnimationProgressed, instead of here.
    ResizeToWidth(target_collapse_state_.uncollapsed_width);
  }
}

void VerticalTabStripRegionView::ResizeToWidth(int width) {
  // The collapsed state of the state controller is used to determine whether
  // the tab strip includes the exclusion zone or is drawn underneath it. So
  // instead of setting it immediately upon starting the resize animation, only
  // do so once it crosses the exclusion width threshold.
  if (!exclusion_width_.has_value() ||
      target_collapse_state_.collapsed == width < exclusion_width_.value()) {
    state_controller_->SetCollapsed(target_collapse_state_.collapsed);
  }

  InvalidateLayout();
}

void VerticalTabStripRegionView::MaybeMoveProfileAndAppMenuButtons() {
#if BUILDFLAG(IS_MAC)
  // Keep the vertical tab strip sidebar "tabs-only" (no toolbar buttons moved
  // into it).
  if (kSkepterSidebarTabsOnly) {
    return;
  }

  if (skepter_moved_profile_and_menu_buttons_) {
    return;
  }

  constexpr int kMaxAttempts = 25;
  if (skepter_move_profile_and_menu_attempts_ >= kMaxAttempts) {
    return;
  }
  skepter_move_profile_and_menu_attempts_++;

  if (!browser_view_ || !bottom_button_container_) {
    return;
  }

  ToolbarButtonProvider* const button_provider =
      browser_view_->toolbar_button_provider();
  if (!button_provider) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &VerticalTabStripRegionView::MaybeMoveProfileAndAppMenuButtons,
            weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  auto* const avatar_button = button_provider->GetAvatarToolbarButton();
  auto* const app_menu_button = button_provider->GetAppMenuButton();
  if (!avatar_button || !app_menu_button) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &VerticalTabStripRegionView::MaybeMoveProfileAndAppMenuButtons,
            weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  skepter_profile_button_ = avatar_button;
  skepter_app_menu_button_ = app_menu_button;

  if (avatar_button->parent() == bottom_button_container_ &&
      app_menu_button->parent() == bottom_button_container_) {
    skepter_moved_profile_and_menu_buttons_ = true;
    UpdateSkepterProfileAndAppMenuVisibility();
    return;
  }

  auto move_view_into_bottom_container = [this](views::View* view) {
    if (!view || view->parent() == bottom_button_container_) {
      return;
    }
    views::View* const parent = view->parent();
    if (!parent) {
      return;
    }
    bottom_button_container_->AddChildView(parent->RemoveChildViewT(view));
    view->SetProperty(views::kMarginsKey, gfx::Insets());
    view->ClearProperty(views::kFlexBehaviorKey);
    view->SetVisible(true);
  };

  // Order matters: profile button should be left of the app menu (3 dots).
  move_view_into_bottom_container(avatar_button);
  move_view_into_bottom_container(app_menu_button);

  if (avatar_button->parent() == bottom_button_container_ &&
      app_menu_button->parent() == bottom_button_container_) {
    skepter_moved_profile_and_menu_buttons_ = true;
    UpdateSkepterProfileAndAppMenuVisibility();
  } else {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &VerticalTabStripRegionView::MaybeMoveProfileAndAppMenuButtons,
            weak_ptr_factory_.GetWeakPtr()));
  }
#endif
}

void VerticalTabStripRegionView::UpdateSkepterProfileAndAppMenuVisibility() {
#if BUILDFLAG(IS_MAC)
  if (!skepter_moved_profile_and_menu_buttons_) {
    return;
  }

  const bool should_show =
      !state_controller_->IsCollapsed() && !state_controller_->IsZenHidden();
  if (skepter_profile_button_) {
    skepter_profile_button_->SetVisible(should_show);
  }
  if (skepter_app_menu_button_) {
    skepter_app_menu_button_->SetVisible(should_show);
  }
#endif
}

void VerticalTabStripRegionView::UpdateColors() {
  top_button_separator_->SetColorId(IsFrameActive()
                                        ? kColorTabDividerFrameActive
                                        : kColorTabDividerFrameInactive);
}

bool VerticalTabStripRegionView::IsFrameActive() const {
  return GetWidget() ? GetWidget()->ShouldPaintAsActive() : true;
}

TabDragTarget* VerticalTabStripRegionView::GetTabDragTarget(
    const gfx::Point& point_in_screen) {
  if (!drag_handler_) {
    return nullptr;
  }
  if (!tab_strip_view_->GetBoundsInScreen().Contains(point_in_screen)) {
    return nullptr;
  }

  // Note: if the drag has not attached to this tab strip yet, it doesn't matter
  // which container is used because the first drag loop iteration just attaches
  // it.
  if (drag_handler_->IsDraggingPinnedTabs()) {
    return &GetPinnedTabsContainer()->GetTabDragTarget(point_in_screen);
  }
  return &GetUnpinnedTabsContainer()->GetTabDragTarget(point_in_screen);
}

BEGIN_METADATA(VerticalTabStripRegionView)
END_METADATA
