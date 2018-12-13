#import <WebKit/WebKit.h>
#import "MWMViewController.h"
#import "MWMTypes.h"

NS_ASSUME_NONNULL_BEGIN

@interface WebViewController : MWMViewController <WKNavigationDelegate>

@property (nonatomic) NSURL * _Nullable m_url;
@property (copy, nonatomic) NSString * _Nullable m_htmlText;
// Set to YES if external browser should be launched
@property (nonatomic) BOOL openInSafari;
@property (nonatomic, readonly) WKWebView * webView;

- (instancetype _Nullable)initWithUrl:(NSURL *)url title:( NSString * _Nullable)title;
- (instancetype _Nullable)initWithHtml:(NSString *)htmlText
                               baseUrl:(NSURL * _Nullable)url
                                 title:(NSString * _Nullable)title;
- (instancetype _Nullable)initWithAuthURL:(NSURL *)url
                            onSuccessAuth:(MWMStringBlock _Nullable)success
                                onFailure:(MWMVoidBlock _Nullable)failure;
- (void)willLoadUrl:(MWMBoolBlock)decisionHandler;
- (void)forward;
- (void)back;

@end

NS_ASSUME_NONNULL_END
