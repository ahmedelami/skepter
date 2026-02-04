// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"

#include "build/build_config.h"
#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/tabs/vertical/top_container_button.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/views/actions/action_view_controller.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/delegating_layout_manager.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_class_properties.h"

VerticalTabStripTopContainer::VerticalTabStripTopContainer(
    tabs::VerticalTabStripStateController* state_controller,
    actions::ActionItem* root_action_item,
    Browser* browser)
    : state_controller_(state_controller),
      root_action_item_(root_action_item),
      browser_(browser),
      action_view_controller_(std::make_unique<views::ActionViewController>()) {
  SetProperty(views::kElementIdentifierKey,
              kVerticalTabStripTopContainerElementId);
  SetLayoutManager(std::make_unique<views::DelegatingLayoutManager>(this));

#if !BUILDFLAG(IS_MAC)
  tab_search_button_ = AddChildButtonFor(kActionTabSearch);
  tab_search_button_->SetProperty(views::kElementIdentifierKey,
                                  kTabSearchButtonElementId);
  collapse_button_ = AddChildButtonFor(kActionToggleCollapseVertical);
  collapse_button_->SetProperty(views::kElementIdentifierKey,
                                kVerticalTabStripCollapseButtonElementId);
#endif

#if BUILDFLAG(IS_MAC)
  auto make_nav_button =
      [&](int command_id, const gfx::VectorIcon& icon,
          std::u16string tooltip_text, std::u16string accessible_name) {
        auto button = std::make_unique<TopContainerButton>();
        button->SetCallback(base::BindRepeating(
            [](Browser* browser, int command_id, const ui::Event& event) {
              chrome::ExecuteCommandWithDisposition(
                  browser, command_id,
                  ui::DispositionFromEventFlags(event.flags()));
            },
            browser_, command_id));
        button->UpdateIcon(ui::ImageModel::FromVectorIcon(icon));
        button->SetText(std::u16string());
        button->SetTooltipText(std::move(tooltip_text));
        button->GetViewAccessibility().SetName(std::move(accessible_name));
        button->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
        button->set_tag(command_id);

        TopContainerButton* button_ptr = AddChildView(std::move(button));
        button_ptr->SetEnabled(chrome::IsCommandEnabled(browser_, command_id));
        chrome::AddCommandObserver(browser_, command_id, this);
        return button_ptr;
      };

  reload_button_ =
      make_nav_button(IDC_RELOAD, vector_icons::kReloadChromeRefreshIcon,
                      l10n_util::GetStringUTF16(IDS_TOOLTIP_RELOAD),
                      l10n_util::GetStringUTF16(IDS_ACCNAME_RELOAD));

  back_button_ =
      make_nav_button(IDC_BACK, vector_icons::kBackArrowChromeRefreshIcon,
                      l10n_util::GetStringUTF16(IDS_TOOLTIP_BACK),
                      l10n_util::GetStringUTF16(IDS_ACCNAME_BACK));
  forward_button_ =
      make_nav_button(IDC_FORWARD, vector_icons::kForwardArrowChromeRefreshIcon,
                      l10n_util::GetStringUTF16(IDS_TOOLTIP_FORWARD),
                      l10n_util::GetStringUTF16(IDS_ACCNAME_FORWARD));
#endif
}

VerticalTabStripTopContainer::~VerticalTabStripTopContainer() {
#if BUILDFLAG(IS_MAC)
  chrome::RemoveCommandObserver(browser_, IDC_BACK, this);
  chrome::RemoveCommandObserver(browser_, IDC_FORWARD, this);
  chrome::RemoveCommandObserver(browser_, IDC_RELOAD, this);
#endif
}

views::ProposedLayout VerticalTabStripTopContainer::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  views::ProposedLayout layout;
  gfx::Size host_size =
      gfx::Size(size_bounds.width().is_bounded() ? size_bounds.width().value()
                                                 : parent()->width(),
                toolbar_height_);

  std::vector<views::LabelButton*> container_buttons;

#if BUILDFLAG(IS_MAC)
  const bool is_collapsed = state_controller_->IsCollapsed();

  if (reload_button_ && reload_button_->GetVisible()) {
    container_buttons.push_back(reload_button_);
  }
  if (back_button_ && back_button_->GetVisible()) {
    container_buttons.push_back(back_button_);
  }
  if (!is_collapsed && forward_button_ && forward_button_->GetVisible()) {
    container_buttons.push_back(forward_button_);
  }
  if (collapse_button_ && collapse_button_->GetVisible()) {
    container_buttons.push_back(collapse_button_);
  }

  const int padding =
      GetLayoutConstant(LayoutConstant::kVerticalTabStripTopButtonPadding);

  if (is_collapsed) {
    // If the vertical tab strip is collapsed, then lay out the buttons
    // vertically from top-to-bottom.
    int total_height = exclusion_width_ == 0 ? 0 : toolbar_height_;
    for (views::LabelButton* container_button : container_buttons) {
      total_height += container_button->GetPreferredSize().height();
    }
    if (container_buttons.size() >= 2) {
      total_height += (container_buttons.size() - 1) * padding;
    }

    if (total_height > host_size.height()) {
      host_size.set_height(total_height);
    }

    int current_y = 0;

    for (views::LabelButton* container_button : container_buttons) {
      const gfx::Size pref_size = container_button->GetPreferredSize();
      gfx::Rect bounds(std::max(0, (host_size.width() - pref_size.width()) / 2),
                       current_y, pref_size.width(), pref_size.height());
      layout.child_layouts.emplace_back(container_button,
                                        container_button->GetVisible(), bounds);

      host_size.SetToMax(gfx::Size(bounds.right(), 0));

      current_y += pref_size.height() + padding;
    }
  } else {
    // Lay out the buttons left-to-right, avoiding the exclusion zone (traffic
    // lights, etc) on the leading edge.
    int current_x =
        exclusion_width_ > 0 ? (exclusion_width_ + padding) : padding;

    for (views::LabelButton* container_button : container_buttons) {
      const gfx::Size pref_size = container_button->GetPreferredSize();
      gfx::Rect bounds(
          current_x,
          std::max(0, (host_size.height() - pref_size.height()) / 2),
          pref_size.width(), pref_size.height());
      layout.child_layouts.emplace_back(container_button,
                                        container_button->GetVisible(), bounds);

      host_size.SetToMax(gfx::Size(bounds.right(), bounds.bottom()));
      current_x += pref_size.width() + padding;
    }
  }

  if (is_collapsed && forward_button_) {
    // Ensure the forward button becomes hidden in the thinnest (collapsed)
    // sidebar state.
    layout.child_layouts.emplace_back(forward_button_.get(),
                                      /*visible=*/false, gfx::Rect());
  }

  layout.host_size = host_size;
  return layout;
#else  // BUILDFLAG(IS_MAC)

  if (tab_search_button_ && tab_search_button_->GetVisible()) {
    container_buttons.push_back(tab_search_button_);
  }
  if (collapse_button_ && collapse_button_->GetVisible()) {
    container_buttons.push_back(collapse_button_);
  }

  const int padding =
      GetLayoutConstant(LayoutConstant::kVerticalTabStripTopButtonPadding);

  if (state_controller_->IsCollapsed()) {
    // If the vertical tab strip is collapsed, then lay out the buttons
    // vertically in reverse order from top-to-bottom.
    int total_height = exclusion_width_ == 0 ? 0 : toolbar_height_;
    for (views::LabelButton* container_button : container_buttons) {
      total_height += container_button->GetPreferredSize().height();
    }
    if (container_buttons.size() >= 2) {
      total_height += (container_buttons.size() - 1) * padding;
    }

    if (total_height > host_size.height()) {
      host_size.set_height(total_height);
    }

    int current_y = 0;

    for (views::LabelButton* container_button :
         base::Reversed(container_buttons)) {
      const gfx::Size pref_size = container_button->GetPreferredSize();
      gfx::Rect bounds(std::max(0, (host_size.width() - pref_size.width()) / 2),
                       current_y, pref_size.width(), pref_size.height());
      layout.child_layouts.emplace_back(container_button,
                                        container_button->GetVisible(), bounds);

      host_size.SetToMax(gfx::Size(bounds.right(), 0));

      current_y += pref_size.height() + padding;
    }
  } else {
    // If the vertical tab strip is uncollapsed, then lay out the buttons
    // horizontally. The exact y-level of the buttons depends on if they can lay
    // on one line or not.
    int total_width = exclusion_width_;
    for (views::LabelButton* container_button : container_buttons) {
      total_width += container_button->GetPreferredSize().width();
    }

    if (container_buttons.size() >= 2) {
      total_width += (container_buttons.size() - 1) * padding;
    }

    // If we're trying to get the minimum size, it will ask for layout for size
    // bounds {0, 0}, but overflow is based on available size.
    const int available_width =
        host_size.width() > 0
            ? host_size.width()
            : parent()->GetAvailableSize(this).width().value_or(0);

    // If there is not enough space for the buttons on a single line with
    // caption buttons, shift them below.
    const bool wrapped_due_to_overflow = size_bounds.width().is_bounded() &&
                                         exclusion_width_ > 0 &&
                                         total_width > available_width;

    if (wrapped_due_to_overflow) {
      host_size.Enlarge(0,
                        GetLayoutConstant(LayoutConstant::kBookmarkBarHeight));

      const int y_baseline =
          toolbar_height_ +
          GetLayoutConstant(LayoutConstant::kBookmarkBarHeight) / 2;

      // If there is not enough space for all of the buttons to be on the same
      // line as the caption buttons, then we lay them out with collapse_button_
      // anchored to the left and tab_search_ on the right.
      if (collapse_button_) {
        const gfx::Size pref_size = collapse_button_->GetPreferredSize();
        gfx::Rect bounds(GetLayoutConstant(
                             LayoutConstant::kVerticalTabStripTopButtonPadding),
                         std::max(0, y_baseline - pref_size.height() / 2),
                         pref_size.width(), pref_size.height());
        layout.child_layouts.emplace_back(
            collapse_button_.get(), collapse_button_->GetVisible(), bounds);
        host_size.SetToMax(gfx::Size(0, bounds.bottom()));
      }

      if (tab_search_button_ && tab_search_button_->GetVisible()) {
        const gfx::Size pref_size = tab_search_button_->GetPreferredSize();
        gfx::Rect bounds(host_size.width() - pref_size.width(),
                         std::max(0, y_baseline - pref_size.height() / 2),
                         pref_size.width(), pref_size.height());
        layout.child_layouts.emplace_back(
            tab_search_button_.get(), tab_search_button_->GetVisible(), bounds);
        host_size.SetToMax(gfx::Size(0, bounds.bottom()));
      }
    } else {
      int current_x = host_size.width();

      // Calculate bounds to right-align the button horizontally and center it
      // vertically within the available space.
      for (views::LabelButton* container_button : container_buttons) {
        const gfx::Size pref_size = container_button->GetPreferredSize();
        gfx::Rect bounds(
            current_x - pref_size.width(),
            std::max(0, (host_size.height() - pref_size.height()) / 2),
            pref_size.width(), pref_size.height());
        layout.child_layouts.emplace_back(
            container_button, container_button->GetVisible(), bounds);

        host_size.SetToMax(gfx::Size(0, bounds.bottom()));

        current_x -= (pref_size.width() +
                      GetLayoutConstant(
                          LayoutConstant::kVerticalTabStripTopButtonPadding));
      }
    }
  }

  layout.host_size = host_size;

  return layout;
#endif  // BUILDFLAG(IS_MAC)
}

views::LabelButton* VerticalTabStripTopContainer::AddChildButtonFor(
    actions::ActionId action_id) {
  std::unique_ptr<TopContainerButton> container_button =
      std::make_unique<TopContainerButton>();
  actions::ActionItem* action_item =
      actions::ActionManager::Get().FindAction(action_id, root_action_item_);
  CHECK(action_item);

  action_view_controller_->CreateActionViewRelationship(
      container_button.get(), action_item->GetAsWeakPtr());

  TopContainerButton* container_button_ptr =
      AddChildView(std::move(container_button));

  container_button_ptr->SetHorizontalAlignment(gfx::ALIGN_RIGHT);

  return container_button_ptr;
}

bool VerticalTabStripTopContainer::IsPositionInWindowCaption(
    const gfx::Point& point) {
  if (tab_search_button_ && tab_search_button_->GetVisible() &&
      IsHitInView(tab_search_button_, point)) {
    return false;
  }

  if (collapse_button_ && collapse_button_->GetVisible() &&
      IsHitInView(collapse_button_, point)) {
    return false;
  }

#if BUILDFLAG(IS_MAC)
  if (back_button_ && back_button_->GetVisible() &&
      IsHitInView(back_button_, point)) {
    return false;
  }

  if (forward_button_ && forward_button_->GetVisible() &&
      IsHitInView(forward_button_, point)) {
    return false;
  }

  if (reload_button_ && reload_button_->GetVisible() &&
      IsHitInView(reload_button_, point)) {
    return false;
  }
#endif

  return true;
}

void VerticalTabStripTopContainer::SetToolbarHeightForLayout(
    const int toolbar_height) {
  if (toolbar_height_ == toolbar_height) {
    return;
  }
  toolbar_height_ = toolbar_height;
  InvalidateLayout();
}

void VerticalTabStripTopContainer::SetExclusionWidthForLayout(
    const int exclusion_width) {
  if (exclusion_width_ == exclusion_width) {
    return;
  }
  exclusion_width_ = exclusion_width;
  InvalidateLayout();
}

void VerticalTabStripTopContainer::EnabledStateChangedForCommand(int id,
                                                                 bool enabled) {
#if BUILDFLAG(IS_MAC)
  switch (id) {
    case IDC_BACK:
      if (back_button_) {
        back_button_->SetEnabled(enabled);
      }
      return;
    case IDC_FORWARD:
      if (forward_button_) {
        forward_button_->SetEnabled(enabled);
      }
      return;
    case IDC_RELOAD:
      if (reload_button_) {
        reload_button_->SetEnabled(enabled);
      }
      return;
    default:
      return;
  }
#else
  return;
#endif
}

BEGIN_METADATA(VerticalTabStripTopContainer)
END_METADATA
