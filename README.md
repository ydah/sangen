# 算言

算言者以漢文記算法之言也
源文綴以 `.kbn` 字用萬國碼第八式
處理系以丙語第九十九式成之

## 造

```sh
make
```

## 用

```sh
./sangen examples/fizzbuzz.kbn
./sangen examples/fizzbuzz.kbn --字碼
./sangen examples/fizzbuzz.kbn --詞
./sangen examples/fizzbuzz.kbn --文樹
./sangen examples/fizzbuzz.kbn --譯=c > fizzbuzz.c
```

## 法

注始於 `注曰` 終於 `注畢`
辭始於 `辭曰` 終於 `辭畢` 欲書 `辭畢` 則重之
數至 `九千九百九十九億九千九百九十九萬九千九百九十九`
二數相算曰 `甲與乙之和`
術受天干 用時取同名之值
`--譯=c` 者譯算言爲丙語也

## 術

```kbn
夫倍者術也
受甲
答甲與二之積
術畢

令甲爲七
書用倍術
```

## 試

```sh
make test
```
