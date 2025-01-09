#include "threadpool.hpp"
#include <iostream>

void sum(int &ans, std::vector<int> &arr) {
  for (int i = 0; i < arr.size(); ++i) {
    ans += arr[i];
  }
}

int main() {
  threadpool::ThreadPool tp(3);
  std::vector<int> s1 = {1, 2, 3};
  int ans1 = 0;

  std::vector<int> s2 = {4, 5};
  int ans2 = 0;

  std::vector<int> s3 = {8, 9, 10};
  int ans3 = 0;

  // добавляем в thread_pool выполняться 3 задачи
  auto id1 = tp.AddTask(sum, std::ref(ans1), std::ref(s1));
  auto id2 = tp.AddTask(sum, std::ref(ans2), std::ref(s2));
  auto id3 = tp.AddTask(sum, std::ref(ans3), std::ref(s3));

  if (tp.IsSubmitted(id1)) {
    // если результат уже посчитан, то просто выводим ответ
    std::cout << ans1 << std::endl;
  } else {
    // если результат ещё не готов, то ждём его
    tp.Wait(id1);
    std::cout << ans1 << std::endl;
  }
  tp.WaitAll();

  std::cout << ans2 << std::endl;
  std::cout << ans3 << std::endl;
  return 0;
}
