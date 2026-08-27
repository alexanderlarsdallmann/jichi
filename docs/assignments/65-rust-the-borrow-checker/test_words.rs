// The suite -- do NOT edit it. It will not even compile until first_word is
// fixed, because the borrow checker rejects the dangling return.
#[path = "words.rs"]
mod words;
use words::first_word;

#[test]
fn first_of_two() {
    assert_eq!(first_word("hello world"), "hello");
}
#[test]
fn single_word() {
    assert_eq!(first_word("solo"), "solo");
}
