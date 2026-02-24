**Switch Statement:**
Alkyl supports `switch` with explicit fallthrough syntax (`leak`).

```alkyl
switch (val) {
    case 1: 
        print("One");
    case 2, 3: 
        print("Two or Three");
    leak case 4: 
        print("Four (leaks to default)");
    default:
        print("Default");
}
```

By default, switch **statements are unleaked**, unless typed as 

```alkyl
leak switch (val) {
    unleak case 0:
        print("Zero (does not leak)");
    case 1: 
        print("One (leaks to all below)");
    case 2, 3: 
        print("Two or Three (leaks to all below)");
    case 4: 
        print("Four (leaks to default)");
    default:
        print("Default");
}
```
