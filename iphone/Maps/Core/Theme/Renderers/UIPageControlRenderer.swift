extension UIPageControl {
  @objc override func applyTheme() {
    if styleName.isEmpty {
      styleName = "PageControl"
    }
    for style in StyleManager.shared.getStyle(styleName) {
      UIPageControlenderer.render(self, style: style)
    }
  }
}

class UIPageControlenderer {
  class func render(_ control: UIPageControl, style: Style) {
    if let backgroundColor = style.backgroundColor {
      control.backgroundColor = backgroundColor
    }
    if let pageIndicatorTintColor = style.pageIndicatorTintColor {
      control.pageIndicatorTintColor = pageIndicatorTintColor
    }
    if let currentPageIndicatorTintColor = style.currentPageIndicatorTintColor {
      control.currentPageIndicatorTintColor = currentPageIndicatorTintColor
    }
  }
}
