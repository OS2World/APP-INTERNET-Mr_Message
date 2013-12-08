#ifndef ACLCTL
#define ACLCTL

#define HRTV ULONG

// #ifdef __CPLUSPLUS
extern "C"
{
// #endif

/* 1 */
/* Initialise the ACL Control Library */
ULONG __pascal ACLInitCtl( void );

/* 2 */
/* Create a new rich text control of the given dimensions */
HRTV __pascal ACLCreateRichTextView( HWND Parent,
                             LONG X,
                             LONG Y,
                             LONG Width,
                             LONG Height,
                             ULONG Visible
                             );

/* 3 */
/* Clear the text in the control */
void __pascal ACLRTVClearText( HRTV rtv );

/* 4 */
/* Add the given text to the end of the control */
void __pascal ACLRTVAddText( HRTV rtv, 
                    	     char* text );

/* 5 */
/* Add the given text as a new paragraph to the control */
/* (ie. append cr+lf as well) */
void __pascal ACLRTVAddParagraph( HRTV rtv, 
                                  char* text );

/* 6 */
/* Add the given text as a new paragraph, and select it visibly */
void __pascal ACLRTVAddSelectedParagraph( HRTV rtv, 
                                          char* text ); 

/* 7 */
/* Returns the window handle of the control */
HWND __pascal ACLRTVGetWindow( HRTV rtv ); 

// #ifdef __CPLUSPLUS
}
// #endif

#endif
