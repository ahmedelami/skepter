// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.h"

#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_utils.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/bookmarks/saved_tab_groups/saved_tab_group_everything_menu.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_flat_edge_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_ink_drop_util.h"
#include "chrome/grit/generated_resources.h"
#include "build/build_config.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/actions/action_view_controller.h"
#include "ui/views/controls/button/label_button_border.h"
#include "ui/views/controls/button/menu_button_controller.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/view.h"

VerticalTabStripBottomContainer::VerticalTabStripBottomContainer(
    tabs::VerticalTabStripStateController* state_controller,
    actions::ActionItem* root_action_item,
    BrowserWindowInterface* browser,
    ButtonSet button_set)
    : root_action_item_(root_action_item),
      button_set_(button_set),
      browser_(browser),
      action_view_controller_(std::make_unique<views::ActionViewController>()) {
  if (button_set_ != ButtonSet::kNewTabOnly) {
    SetProperty(views::kElementIdentifierKey,
                kVerticalTabStripBottomContainerElementId);
  }

  collapsed_state_changed_subscription_ =
      state_controller->RegisterOnCollapseChanged(base::BindRepeating(
          &VerticalTabStripBottomContainer::OnCollapsedStateChanged,
          base::Unretained(this)));

  if (button_set_ == ButtonSet::kNewTabAndTabGroup ||
      button_set_ == ButtonSet::kTabGroupOnly) {
    if (tabs::IsProjectsPanelFeatureEnabled()) {
      tab_group_button_ = AddChildButtonFor(kActionToggleProjectsPanel);
      tab_group_button_->SetProperty(views::kElementIdentifierKey,
                                     kVerticalTabStripProjectsButtonElementId);
    } else if (tab_groups::SavedTabGroupUtils::IsEnabledForProfile(
                   browser_->GetProfile())) {
      tab_group_button_ = AddChildButtonFor(kActionTabGroupsMenu);

      // Creating MenuButtonController because tab_group_button is a LabelButton.
      auto controller = std::make_unique<views::MenuButtonController>(
          tab_group_button_,
          base::BindRepeating(
              &VerticalTabStripBottomContainer::ShowEverythingMenu,
              base::Unretained(this)),
          std::make_unique<views::Button::DefaultButtonControllerDelegate>(
              tab_group_button_));
      everything_menu_controller_ = controller.get();

      tab_group_button_->SetButtonController(std::move(controller));
      tab_group_button_->SetProperty(views::kElementIdentifierKey,
                                     kSavedTabGroupButtonElementId);
    }
  }

#if BUILDFLAG(IS_MAC)
  if (button_set_ == ButtonSet::kTabGroupOnly) {
    // Reserve space to place toolbar actions (profile + menu) to the right of
    // the tab groups button.
    skepter_trailing_spacer_ = AddChildView(std::make_unique<views::View>());
    skepter_trailing_spacer_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                                 views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kUnbounded));
  }
#endif

  if (button_set_ == ButtonSet::kNewTabAndTabGroup ||
      button_set_ == ButtonSet::kNewTabOnly) {
    new_tab_button_ = AddChildButtonFor(kActionNewTab);
    new_tab_button_->SetProperty(views::kElementIdentifierKey,
                                 kNewTabButtonElementId);
    new_tab_button_->SetHorizontalAlignment(
        gfx::HorizontalAlignment::ALIGN_LEFT);
    new_tab_button_->SetElideBehavior(gfx::ElideBehavior::ELIDE_TAIL);

    new_tab_full_text_ = l10n_util::GetStringUTF16(IDS_NEW_TAB);
    new_tab_short_text_ = new_tab_full_text_;
    if (const size_t first_space = new_tab_full_text_.find(u' ');
        first_space != std::u16string::npos) {
      new_tab_short_text_ = new_tab_full_text_.substr(0, first_space);
    }
    new_tab_button_->SetText(new_tab_full_text_);
  }

  UpdateButtonStyles(state_controller);
}

VerticalTabStripBottomContainer::~VerticalTabStripBottomContainer() = default;

VerticalTabStripFlatEdgeButton*
VerticalTabStripBottomContainer::AddChildButtonFor(
    actions::ActionId action_id) {
  std::unique_ptr<VerticalTabStripFlatEdgeButton> container_button =
      std::make_unique<VerticalTabStripFlatEdgeButton>();
  actions::ActionItem* action_item =
      actions::ActionManager::Get().FindAction(action_id, root_action_item_);
  CHECK(action_item);

  action_view_controller_->CreateActionViewRelationship(
      container_button.get(), action_item->GetAsWeakPtr());

  VerticalTabStripFlatEdgeButton* raw_container_button =
      AddChildView(std::move(container_button));

  raw_container_button->SetHorizontalAlignment(
      gfx::HorizontalAlignment::ALIGN_CENTER);

  return raw_container_button;
}

void VerticalTabStripBottomContainer::ShowEverythingMenu() {
  if (everything_menu_ && everything_menu_->IsShowing()) {
    return;
  }

  // Creating everything menu.
  everything_menu_ = std::make_unique<tab_groups::STGEverythingMenu>(
      everything_menu_controller_, browser_->GetBrowserForMigrationOnly(),
      tab_groups::STGEverythingMenu::MenuContext::kVerticalTabStrip);

  everything_menu_->RunMenu();
}

void VerticalTabStripBottomContainer::OnCollapsedStateChanged(
    tabs::VerticalTabStripStateController* controller) {
  UpdateButtonStyles(controller);
}

void VerticalTabStripBottomContainer::Layout(PassKey) {
  LayoutSuperclass<views::FlexLayoutView>(this);
  UpdateNewTabButtonTextForWidth();
}

void VerticalTabStripBottomContainer::UpdateButtonStyles(
    tabs::VerticalTabStripStateController* controller) {
  bool is_collapsed = controller->IsCollapsed();

  auto orientation = is_collapsed ? views::LayoutOrientation::kVertical
                                  : views::LayoutOrientation::kHorizontal;

  // Setting button's layout based on collapsed state
  SetOrientation(orientation);

#if BUILDFLAG(IS_MAC)
  if (skepter_trailing_spacer_) {
    skepter_trailing_spacer_->SetVisible(!is_collapsed);
  }
#endif

  // If collapsed, the tab group button and the new tab button share the same
  // weights. The flat edge is inverse to the position: tab group button is
  // placed on top so the flat edge is on the bottom.
  // Flat edges should be reset and padding is moved from top to left.

  // If in incognito mode, the tab groups button will not be visible.
  if (tab_group_button_) {
    tab_group_button_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(orientation,
                                 views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kPreferred, false,
                                 views::MinimumFlexSizeRule::kPreferred));
    tab_group_button_->SetFlatEdge(
        is_collapsed ? VerticalTabStripFlatEdgeButton::FlatEdge::kBottom
                     : VerticalTabStripFlatEdgeButton::FlatEdge::kNone);
    gfx::Insets tab_group_insets = GetLayoutInsets(
        is_collapsed
            ? LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_COLLAPSED
            : LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_UNCOLLAPSED);
    if (button_set_ == ButtonSet::kTabGroupOnly) {
      const int vertical_padding =
          (tab_group_insets.top() + tab_group_insets.bottom()) / 2;
      tab_group_insets.set_left(vertical_padding);
      tab_group_insets.set_right(vertical_padding);
    }
    tab_group_button_->SetInsets(tab_group_insets);
  }

  if (new_tab_button_) {
    new_tab_button_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(
            orientation, views::MinimumFlexSizeRule::kScaleToMinimum,
            is_collapsed ? views::MaximumFlexSizeRule::kPreferred
                         : views::MaximumFlexSizeRule::kUnbounded,
            false, views::MinimumFlexSizeRule::kPreferred));
    new_tab_button_->SetFlatEdge(
        is_collapsed ? VerticalTabStripFlatEdgeButton::FlatEdge::kTop
                     : VerticalTabStripFlatEdgeButton::FlatEdge::kNone);
    int padding = GetLayoutConstant(
        LayoutConstant::kVerticalTabStripCollapsedBottomButtonPadding);
    new_tab_button_->SetProperty(
        views::kMarginsKey, gfx::Insets::TLBR(is_collapsed ? padding : 0,
                                              is_collapsed ? 0 : padding, 0,
                                              0));
    new_tab_button_->SetInsets(GetLayoutInsets(
        is_collapsed
            ? LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_COLLAPSED
            : LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_UNCOLLAPSED));

    RecalculateNewTabTextWidths();
  }
}

void VerticalTabStripBottomContainer::RecalculateNewTabTextWidths() {
  if (!new_tab_button_) {
    return;
  }

  const std::u16string previous_text(new_tab_button_->GetText());

  new_tab_button_->SetText(new_tab_full_text_);
  new_tab_full_width_ = new_tab_button_->GetPreferredSize().width();

  new_tab_button_->SetText(new_tab_short_text_);
  new_tab_short_width_ = new_tab_button_->GetPreferredSize().width();

  new_tab_button_->SetText(previous_text);
}

void VerticalTabStripBottomContainer::UpdateNewTabButtonTextForWidth() {
  if (!new_tab_button_ || !new_tab_button_->GetVisible()) {
    return;
  }

  // Use a small hysteresis to avoid oscillation around the width thresholds.
  constexpr int kHysteresisPx = 8;
  const int available_width = new_tab_button_->width();

  if (available_width <= 0 || new_tab_full_width_ <= 0 ||
      new_tab_short_width_ <= 0) {
    return;
  }

  NewTabTextMode desired_mode = new_tab_text_mode_;

  switch (new_tab_text_mode_) {
    case NewTabTextMode::kFull:
      if (available_width < new_tab_full_width_ - kHysteresisPx) {
        desired_mode = NewTabTextMode::kShort;
      }
      break;
    case NewTabTextMode::kShort:
      if (available_width >= new_tab_full_width_ + kHysteresisPx) {
        desired_mode = NewTabTextMode::kFull;
      } else if (available_width < new_tab_short_width_ - kHysteresisPx) {
        desired_mode = NewTabTextMode::kIconOnly;
      }
      break;
    case NewTabTextMode::kIconOnly:
      if (available_width >= new_tab_short_width_ + kHysteresisPx) {
        desired_mode = NewTabTextMode::kShort;
      }
      break;
  }

  if (desired_mode == new_tab_text_mode_) {
    return;
  }

  new_tab_text_mode_ = desired_mode;
  switch (new_tab_text_mode_) {
    case NewTabTextMode::kFull:
      new_tab_button_->SetText(new_tab_full_text_);
      break;
    case NewTabTextMode::kShort:
      new_tab_button_->SetText(new_tab_short_text_);
      break;
    case NewTabTextMode::kIconOnly:
      new_tab_button_->SetText(std::u16string());
      break;
  }
}

BEGIN_METADATA(VerticalTabStripBottomContainer)
END_METADATA
