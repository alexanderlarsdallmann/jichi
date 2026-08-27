// Return the first whitespace-separated word of s, borrowed from s.
//
// This does NOT COMPILE, and that is the whole lesson: it builds a fresh local
// String and tries to return a slice that points INTO it -- but the local is
// destroyed when the function returns, so the slice would dangle. In C that is a
// use-after-free you find later with a sanitizer (task 51). Rust's borrow
// checker refuses to compile it at all: the error is
//   error[E0515]: cannot return value referencing local variable `owned`.
//
// The fix is not to fight the checker but to listen to it: the caller already
// owns the string data behind `s`, so slice THAT (its lifetime outlives the
// call) instead of making a throwaway copy.
pub fn first_word(s: &str) -> &str {
    let owned = s.to_string();
    owned.split(' ').next().unwrap()
}
