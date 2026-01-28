// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_VERTICAL_VERTICAL_TAB_STRIP_BOTTOM_CONTAINER_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_VERTICAL_VERTICAL_TAB_STRIP_BOTTOM_CONTAINER_H_

#include <string>

#include "ui/views/layout/flex_layout_view.h"

class BrowserWindowInterface;
class VerticalTabStripFlatEdgeButton;

namespace tabs {
class VerticalTabStripStateController;
}  // namespace tabs

namespace tab_groups {
class STGEverythingMenu;
}  // namespace tab_groups

namespace views {
class ActionViewController;
class MenuButtonController;
}  // namespace views

// Bottom container of the vertical tab strip, manages the new tab and tab group
// buttons.
class VerticalTabStripBottomContainer : public views::FlexLayoutView {
  METADATA_HEADER(VerticalTabStripBottomContainer, views::View)
 public:
  enum class ButtonSet {
    kNewTabAndTabGroup,
    kNewTabOnly,
    kTabGroupOnly,
  };

  VerticalTabStripBottomContainer(
      tabs::VerticalTabStripStateController* state_controller,
      actions::ActionItem* root_action_item,
      BrowserWindowInterface* browser,
      ButtonSet button_set = ButtonSet::kNewTabAndTabGroup);
  ~VerticalTabStripBottomContainer() override;

  VerticalTabStripFlatEdgeButton* AddChildButtonFor(
      actions::ActionId action_id);

  void ShowEverythingMenu();

 void OnCollapsedStateChanged(
      tabs::VerticalTabStripStateController* state_controller);

 private:
  // views::View:
  void Layout(PassKey) override;

  void UpdateButtonStyles(
      tabs::VerticalTabStripStateController* state_controller);
  void RecalculateNewTabTextWidths();
  void UpdateNewTabButtonTextForWidth();

  enum class NewTabTextMode { kIconOnly, kShort, kFull };

  raw_ptr<actions::ActionItem> root_action_item_ = nullptr;
  const ButtonSet button_set_;
  raw_ptr<VerticalTabStripFlatEdgeButton> new_tab_button_ = nullptr;
  raw_ptr<VerticalTabStripFlatEdgeButton> tab_group_button_ = nullptr;
  raw_ptr<BrowserWindowInterface> browser_ = nullptr;
  raw_ptr<views::MenuButtonController> everything_menu_controller_ = nullptr;
  base::CallbackListSubscription collapsed_state_changed_subscription_;

  std::unique_ptr<tab_groups::STGEverythingMenu> everything_menu_;
  std::unique_ptr<views::ActionViewController> action_view_controller_;

  std::u16string new_tab_full_text_;
  std::u16string new_tab_short_text_;
  int new_tab_full_width_ = 0;
  int new_tab_short_width_ = 0;
  NewTabTextMode new_tab_text_mode_ = NewTabTextMode::kFull;
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_VERTICAL_VERTICAL_TAB_STRIP_BOTTOM_CONTAINER_H_
